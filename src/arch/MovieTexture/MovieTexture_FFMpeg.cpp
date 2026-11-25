#include "global.h"
#include "MovieTexture_FFMpeg.h"

#include "RageDisplay.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "RageFile.h"
#include "RageSurface.h"

#include <chrono>
#include <cstdint>
#include <thread>

static void FixLilEndian()
{
	if constexpr (!Endian::little) {
		return;
	}

	static bool Initialized = false;
	if (Initialized)
		return;
	Initialized = true;

	for (int i = 0; i < AVPixelFormats[i].bpp; ++i)
	{
		AVPixelFormat_t& pf = AVPixelFormats[i];

		if (!pf.bByteSwapOnLittleEndian)
			continue;

		for (int mask = 0; mask < 4; ++mask)
		{
			int m = pf.masks[mask];
			switch (pf.bpp)
			{
			case 24: m = Swap24(m); break;
			case 32: m = Swap32(m); break;
			default:
				FAIL_M(ssprintf("Unsupported BPP value: %i", pf.bpp));
			}
			pf.masks[mask] = m;
		}
	}
}

static int FindCompatibleAVFormat(bool bHighColor)
{
	for (int i = 0; AVPixelFormats[i].bpp; ++i)
	{
		AVPixelFormat_t& fmt = AVPixelFormats[i];
		if (fmt.YUV != PixelFormatYCbCr_Invalid)
		{
			EffectMode em = MovieTexture_Generic::GetEffectMode(fmt.YUV);
			if (true || !DISPLAY->IsEffectModeSupported(em))
				continue;
		}
		else if (fmt.bHighColor != bHighColor)
		{
			continue;
		}

		RagePixelFormat pixfmt = DISPLAY->FindPixelFormat(fmt.bpp,
			fmt.masks[0],
			fmt.masks[1],
			fmt.masks[2],
			fmt.masks[3],
			true /* realtime */
		);

		if (pixfmt == RagePixelFormat_Invalid)
			continue;

		return i;
	}

	return -1;
}

RageSurface* RageMovieTextureDriver_FFMpeg::AVCodecCreateCompatibleSurface(int iTextureWidth, int iTextureHeight, bool bPreferHighColor, int& iAVTexfmt, MovieDecoderPixelFormatYCbCr& fmtout)
{
	FixLilEndian();

	int iAVTexfmtIndex = FindCompatibleAVFormat(bPreferHighColor);
	if (iAVTexfmtIndex == -1)
		iAVTexfmtIndex = FindCompatibleAVFormat(!bPreferHighColor);

	if (iAVTexfmtIndex == -1)
	{
		/* No dice.  Use the first avcodec format of the preferred bit depth,
		 * and let the display system convert. */
		for (iAVTexfmtIndex = 0; AVPixelFormats[iAVTexfmtIndex].bpp; ++iAVTexfmtIndex)
			if (AVPixelFormats[iAVTexfmtIndex].bHighColor == bPreferHighColor)
				break;
		ASSERT(AVPixelFormats[iAVTexfmtIndex].bpp != 0);
	}

	const AVPixelFormat_t* pfd = &AVPixelFormats[iAVTexfmtIndex];
	iAVTexfmt = pfd->pf;
	fmtout = pfd->YUV;

	LOG->Trace("Texture pixel format: %i %i (%ibpp, %08x %08x %08x %08x)", iAVTexfmt, fmtout,
		pfd->bpp, pfd->masks[0], pfd->masks[1], pfd->masks[2], pfd->masks[3]);

	if (pfd->YUV == PixelFormatYCbCr_YUYV422)
		iTextureWidth /= 2;

	return CreateSurface(iTextureWidth, iTextureHeight, pfd->bpp,
		pfd->masks[0], pfd->masks[1], pfd->masks[2], pfd->masks[3]);
}

MovieDecoder_FFMpeg::MovieDecoder_FFMpeg()
{
	FixLilEndian();

	av_format_context_ = nullptr;
	av_stream_ = nullptr;
	current_packet_offset_ = -1;

	Init();
}

MovieDecoder_FFMpeg::~MovieDecoder_FFMpeg()
{
	if (av_sws_context_)
	{
		avcodec::sws_freeContext(av_sws_context_);
		av_sws_context_ = nullptr;
	}
	if (av_io_context_ != nullptr)
	{
		RageFile* file = (RageFile*)av_io_context_->opaque;
		file->Close();
		delete file;
		avcodec::av_free(av_io_context_);
	}
	if (av_buffer_ != nullptr)
	{
		avcodec::av_free(av_buffer_);
	}
	if (av_stream_codec_ != nullptr)
	{
		avcodec::avcodec_free_context(&av_stream_codec_);
	}
	frame_buffer_.clear();
}

namespace
{
	// How many decoded frames to keep ahead of the one on screen. Frames are
	// stored uncompressed (1080p yuv420p is ~3.1 MB each), so this caps memory
	// per movie at roughly 190 MB for 1080p and 85 MB for 720p, independent of
	// the movie's length. 60 frames is 2 s at 30 fps, 1 s at 60 fps.
	const int kMaxFramesAhead = 60;
}

void MovieDecoder_FFMpeg::Init()
{
	std::lock_guard<std::mutex> lk(buffer_mutex_);
	end_of_file_ = 0;
	display_frame_num_ = 0;
	total_frames_ = 0;
	buffer_base_ = 0;
	frame_buffer_.clear();
	decoding_frame_.reset();
	last_timestamp_ = 0;
	last_delay_ = 0;
	av_sws_context_ = nullptr;
	av_io_context_ = nullptr;
	av_buffer_ = nullptr;
}

FrameHolder* MovieDecoder_FFMpeg::FrameAtLocked(int n) const
{
	const int idx = n - buffer_base_;
	if (idx < 0 || idx >= static_cast<int>(frame_buffer_.size())) {
		return nullptr;
	}
	return frame_buffer_[idx].get();
}

void MovieDecoder_FFMpeg::FreeShownFramesLocked()
{
	bool freed = false;
	while (!frame_buffer_.empty() && buffer_base_ < display_frame_num_) {
		frame_buffer_.pop_front();
		++buffer_base_;
		freed = true;
	}
	if (freed) {
		buffer_cv_.notify_all(); // room for the decode thread
	}
}

void MovieDecoder_FFMpeg::Cancel()
{
	std::lock_guard<std::mutex> lk(buffer_mutex_);
	cancel_ = true;
	buffer_cv_.notify_all();
}

float MovieDecoder_FFMpeg::GetTimestamp() const
{
	std::lock_guard<std::mutex> lk(buffer_mutex_);
	// Always display the first frame.
	if (display_frame_num_ == 0) {
		return 0;
	}
	// If the frame isn't decoded yet, display is outpacing decoding.
	const FrameHolder* f = FrameAtLocked(display_frame_num_);
	return f != nullptr ? f->frame_timestamp : 0;
}

bool MovieDecoder_FFMpeg::IsCurrentFrameReady() {
	std::lock_guard<std::mutex> lk(buffer_mutex_);
	const FrameHolder* f = FrameAtLocked(display_frame_num_);
	// We're displaying faster than decoding. Do not even try to display the frame.
	if (f == nullptr) {
		return false;
	}
	if (f->skip) {
		LOG->Info("Frame %i not decoded, skipping...", display_frame_num_);
		return true;
	}
	// At EOF every buffered frame has been decoded or marked to skip.
	if (end_of_file_) {
		return true;
	}
	return f->decoded;
}

int MovieDecoder_FFMpeg::DecodeNextFrame()
{
	if (cancel_) {
		return -2;
	}

	// Add a new FrameHolder entry. It's shared so that a Rewind() clearing the
	// buffer mid-decode can't free the frame out from under us; frames from a
	// stale generation are simply dropped.
	auto holder = std::make_shared<FrameHolder>();
	int generation;
	{
		std::lock_guard<std::mutex> lk(buffer_mutex_);
		if (end_of_file_) {
			return 0;
		}
		generation = generation_;
		frame_buffer_.push_back(holder);
	}
	decoding_frame_ = holder;

	int status = SendPacketToBuffer();
	if (status < 0) {
		decoding_frame_.reset();
		return status;
	}
	if (status == 0) {
		// EOF. Don't display an EoF frame, and record the real frame count
		// (the count from the container may have been an estimate).
		std::lock_guard<std::mutex> lk(buffer_mutex_);
		if (generation == generation_) {
			if (!frame_buffer_.empty() && frame_buffer_.back() == holder) {
				frame_buffer_.pop_back();
			}
			end_of_file_ = 1;
			total_frames_ = buffer_base_ + static_cast<int>(frame_buffer_.size());
		}
		decoding_frame_.reset();
		return 0;
	}

	status = DecodePacketInBuffer();
	first_frame_ = false;
	decoding_frame_.reset();
	return status;
}

int MovieDecoder_FFMpeg::DecodeMovie()
{
	using std::chrono::operator""ms;

	// The first frame is decoded and drawn already by MovieTexture_Generic::Init().
	while (true)
	{
		{
			std::unique_lock<std::mutex> lk(buffer_mutex_);
			// Sleep while the buffer is full, or at EOF waiting for a loop.
			buffer_cv_.wait(lk, [this] {
				return cancel_ || rewind_requested_ ||
					(!end_of_file_ &&
					 buffer_base_ + static_cast<int>(frame_buffer_.size()) - display_frame_num_ < kMaxFramesAhead);
			});
			if (cancel_) {
				return -2;
			}
			if (rewind_requested_) {
				rewind_requested_ = false;
				lk.unlock();
				SeekToStart();
				continue;
			}
		}

		// The 1ms wait is arbitrary: decode at most ~1000 fps so filling the
		// buffer never starves gameplay of CPU.
		auto wake_up = std::chrono::steady_clock::now() + 1ms;

		int status = DecodeNextFrame();
		// If cancelled (quitting a song, scrolling the banner), or fatal error,
		// stop decoding.
		if (status < 0) {
			return status;
		}

		{
			// When opening the file, fewer frames may have been detected than
			// there actually are. Keep the count ahead of what's been decoded so
			// display doesn't stop early.
			std::lock_guard<std::mutex> lk(buffer_mutex_);
			const int decoded = buffer_base_ + static_cast<int>(frame_buffer_.size());
			if (!end_of_file_ && decoded > total_frames_) {
				total_frames_ = decoded;
			}
		}

		std::this_thread::sleep_until(wake_up);
	}
}

void MovieDecoder_FFMpeg::SeekToStart()
{
	const std::int64_t start = av_stream_->start_time != AV_NOPTS_VALUE ? av_stream_->start_time : 0;
	int ret = avcodec::av_seek_frame(av_format_context_, av_stream_->index, start, AVSEEK_FLAG_BACKWARD);
	if (ret < 0) {
		// Some containers (e.g. MPEG program streams) seek better by bytes.
		ret = avcodec::av_seek_frame(av_format_context_, -1, 0, AVSEEK_FLAG_BYTE);
	}
	if (ret < 0) {
		LOG->Warn("MovieDecoder_FFMpeg: couldn't seek to start for looping (%i)", ret);
	}
	avcodec::avcodec_flush_buffers(av_stream_codec_);
	current_packet_offset_ = -1;
	first_frame_ = true;
	last_timestamp_ = 0;
	last_delay_ = 0;
}

int MovieDecoder_FFMpeg::SendPacketToBuffer()
{
	if (cancel_) {
		return -2;
	}
	if (end_of_file_ > 0) {
		return 0;
	}

	FrameHolder& fh = *decoding_frame_;
	while (true)
	{
		int ret = avcodec::av_read_frame(av_format_context_, fh.packet);
		/* XXX: why is avformat returning AVERROR_NOMEM on EOF? */
		if (ret < 0)
		{
			/* EOF. The caller records it. */
			return 0;
		}

		if (fh.packet->stream_index == av_stream_->index)
		{
			current_packet_offset_ = 0;
			return 1;
		}
		/* It's not for the video stream; ignore it. */
		avcodec::av_packet_unref(fh.packet);
	}
}

int MovieDecoder_FFMpeg::DecodePacketInBuffer() {
	if (cancel_) {
		return -2;
	}
	if (end_of_file_ == 0 && current_packet_offset_ == -1) {
		return 0; /* no packet */
	}

	FrameHolder& fh = *decoding_frame_;
	while (end_of_file_ == 0 && current_packet_offset_ <= fh.packet->size)
	{
		/* If we have no data on the first frame, just return EOF; passing an empty packet
		 * to avcodec_decode_video in this case is crashing it.  However, passing an empty
		 * packet is normal with B-frames, to flush.  This may be unnecessary in newer
		 * versions of avcodec, but I'm waiting until a new stable release to upgrade. */
		if (fh.packet->size == 0 && first_frame_) {
			return 0; /* eof */
		}

		/* Hack: we need to send size = 0 to flush frames at the end, but we have
		 * to give it a buffer to read from since it tries to read anyway. */
		fh.packet->data = fh.packet->size ? fh.packet->data : nullptr;
		int len = fh.packet->size;
		avcodec::avcodec_send_packet(av_stream_codec_, fh.packet);
		int avcodec_return = avcodec::avcodec_receive_frame(av_stream_codec_, fh.frame);

		if (len < 0)
		{
			LOG->Warn("avcodec_decode_video2 fatal error, packet size negative: %i", len);
			return -1;
		}

		current_packet_offset_ += len;

		if (avcodec_return != 0)
		{
			// AVERROR(EAGAIN) is normal while the decoder fills up on the first
			// packets (B-frame delay); the frame is skipped.
			if (avcodec_return != AVERROR(EAGAIN)) {
				LOG->Warn("Frame not successfully decoded into buffer. avcodec_receive_frame status: %i", avcodec_return);
			}
			continue;
		}

		if (fh.frame->pkt_dts != AV_NOPTS_VALUE)
		{
			fh.frame_timestamp = (float)(fh.frame->pkt_dts * av_q2d(av_stream_->time_base));
		}
		else
		{
			/* If the timestamp is zero, this frame is to be played at the
			 * time of the last frame plus the length of the last frame. */
			fh.frame_timestamp = first_frame_ ? 0 : last_timestamp_ + last_delay_;
		}

		// Length of this frame, only used as a fallback for getting the frame
		// timestamp above.
		fh.frame_delay = (float)av_q2d(av_stream_->time_base);
		fh.frame_delay += fh.frame->repeat_pict * (fh.frame_delay * 0.5f);
		last_timestamp_ = fh.frame_timestamp;
		last_delay_ = fh.frame_delay;
		fh.decoded = true;

		return 1;
	}

	// This if statement means the packet did not decode correctly. This is not
	// necessarily fatal for video playback, but out of caution the frame should
	// be skipped.
	if (!fh.decoded) {
		fh.skip = true;
	}

	return 0; /* packet done */
}

bool MovieDecoder_FFMpeg::SkipNextFrame() {
	std::lock_guard<std::mutex> lk(buffer_mutex_);
	if (display_frame_num_ > (total_frames_ - 1)) {
		return true;
	}
	const FrameHolder* f = FrameAtLocked(display_frame_num_);
	if (f != nullptr && f->skip) {
		display_frame_num_++;
		FreeShownFramesLocked();
		return true;
	}
	return false;
}

bool MovieDecoder_FFMpeg::GetFrame(RageSurface* pSurface)
{
	FrameHolder* f;
	{
		std::lock_guard<std::mutex> lk(buffer_mutex_);
		f = FrameAtLocked(display_frame_num_);
	}
	// Only this (display) thread removes frames from the buffer, so f stays
	// valid while we convert it.
	if (f == nullptr) {
		return end_of_file_;
	}

	avcodec::AVFrame pict;
	pict.data[0] = (unsigned char*)pSurface->pixels;
	pict.linesize[0] = pSurface->pitch;

	/* XXX 1: Do this in one of the Open() methods instead?
	 * XXX 2: The problem of doing this in Open() is that m_AVTexfmt is not
	 * already initialized with its correct value.
	 */
	if (av_sws_context_ == nullptr)
	{
		av_sws_context_ = avcodec::sws_getCachedContext(av_sws_context_,
			GetWidth(), GetHeight(), av_stream_codec_->pix_fmt,
			GetWidth(), GetHeight(), av_pixel_format_,
			kSwsFlags, nullptr, nullptr, nullptr);
		if (av_sws_context_ == nullptr)
		{
			LOG->Warn("Cannot initialize sws conversion context for (%d,%d) %d->%d", GetWidth(), GetHeight(), av_stream_codec_->pix_fmt, av_pixel_format_);
			return false;
		}
	}

	avcodec::sws_scale(av_sws_context_,
		f->frame->data, f->frame->linesize, 0, GetHeight(),
		pict.data, pict.linesize);

	std::lock_guard<std::mutex> lk(buffer_mutex_);
	// Don't advance the frame number past the (potential) end of the buffer.
	// This can happen if display is outpacing decoding, or if we're at the
	// end of file.
	if (display_frame_num_ >= (total_frames_ - 1)) {
		return end_of_file_;
	}
	display_frame_num_++;
	FreeShownFramesLocked();
	return false;
}

static RString averr_ssprintf(int err, const char* fmt, ...)
{
	ASSERT(err < 0);

	va_list     va;
	va_start(va, fmt);
	RString s = vssprintf(fmt, va);
	va_end(va);

	std::size_t errbuf_size = 512;
	char* errbuf = new char[errbuf_size];
	avcodec::av_strerror(err, errbuf, errbuf_size);
	RString Error = ssprintf("%i: %s", err, errbuf);
	delete[] errbuf;

	return s + " (" + Error + ")";
}

static int AVIORageFile_ReadPacket(void* opaque, std::uint8_t* buf, int buf_size)
{
	RageFile* f = (RageFile*)opaque;
	int n = f->Read(buf, buf_size);
	if (n == 0)
		return AVERROR_EOF;
	return n;
}

static std::int64_t AVIORageFile_Seek(void* opaque, std::int64_t offset, int whence)
{
	RageFile* f = (RageFile*)opaque;
	if (whence == AVSEEK_SIZE)
		return f->GetFileSize();

	if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END)
	{
		LOG->Trace("Error: unsupported seek whence: %d", whence);
		return -1;
	}

	return f->Seek((int)offset, whence);
}

RString MovieDecoder_FFMpeg::Open(RString sFile)
{
	av_format_context_ = avcodec::avformat_alloc_context();
	if (!av_format_context_)
		return "AVCodec: Couldn't allocate context";

	RageFile* f = new RageFile;

	if (!f->Open(sFile, RageFile::READ))
	{
		RString errorMessage = f->GetError();
		RString error = ssprintf("MovieDecoder_FFMpeg: Error opening \"%s\": %s", sFile.c_str(), errorMessage.c_str());
		delete f;
		return error;
	}

	av_buffer_ = (unsigned char*)avcodec::av_malloc(STEPMANIA_FFMPEG_BUFFER_SIZE);
	av_io_context_ = avcodec::avio_alloc_context(av_buffer_, STEPMANIA_FFMPEG_BUFFER_SIZE, 0, f, AVIORageFile_ReadPacket, nullptr, AVIORageFile_Seek);
	av_format_context_->pb = av_io_context_;
	int ret = avcodec::avformat_open_input(&av_format_context_, sFile.c_str(), nullptr, nullptr);
	if (ret < 0)
		return RString(averr_ssprintf(ret, "AVCodec: Couldn't open \"%s\"", sFile.c_str()));

	ret = avcodec::avformat_find_stream_info(av_format_context_, nullptr);
	if (ret < 0)
		return RString(averr_ssprintf(ret, "AVCodec (%s): Couldn't find codec parameters", sFile.c_str()));

	int stream_idx = avcodec::av_find_best_stream(av_format_context_, avcodec::AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	if (stream_idx < 0 ||
		static_cast<unsigned int>(stream_idx) >= av_format_context_->nb_streams ||
		av_format_context_->streams[stream_idx] == nullptr)
		return "Couldn't find any video streams";
	av_stream_ = av_format_context_->streams[stream_idx];
	av_stream_codec_ = avcodec::avcodec_alloc_context3(nullptr);
	if (avcodec::avcodec_parameters_to_context(av_stream_codec_, av_stream_->codecpar) < 0)
		return ssprintf("Could not get context from parameters");

	if (av_stream_codec_->codec_id == avcodec::AV_CODEC_ID_NONE)
		return ssprintf("Unsupported codec %08x", av_stream_codec_->codec_tag);

	RString sError = OpenCodec();
	if (!sError.empty())
		return ssprintf("AVCodec (%s): %s", sFile.c_str(), sError.c_str());

	LOG->Trace("Bitrate: %i", static_cast<int>(av_stream_codec_->bit_rate));
	LOG->Trace("Codec pixel format: %s", avcodec::av_get_pix_fmt_name(av_stream_codec_->pix_fmt));
	total_frames_ = av_stream_->nb_frames;
	if (total_frames_ <= 0) {
		// Sometimes we might not get a correct frame count.
		// In that case, approximate and fix it later.
		total_frames_ = av_format_context_->duration // microseconds
			* (av_stream_->avg_frame_rate.num) / (av_stream_->avg_frame_rate.den) / (1000000);
		LOG->Trace("Number of frames provided is inaccurate, estimating.");
	}
	LOG->Trace("Number of frames detected: %i", total_frames_);

	return RString();
}

RString MovieDecoder_FFMpeg::OpenCodec()
{
	Init();

	ASSERT(av_stream_ != nullptr);
	if (av_stream_codec_->codec)
		avcodec::avcodec_close(av_stream_codec_);

	const avcodec::AVCodec* pCodec = avcodec::avcodec_find_decoder(av_stream_codec_->codec_id);
	if (pCodec == nullptr)
		return ssprintf("Couldn't find decoder %i", av_stream_codec_->codec_id);

	av_stream_codec_->workaround_bugs = 1;
	av_stream_codec_->idct_algo = FF_IDCT_AUTO;
	av_stream_codec_->error_concealment = 3;

	LOG->Trace("Opening codec %s", pCodec->name);

	int ret = avcodec::avcodec_open2(av_stream_codec_, pCodec, nullptr);
	if (ret < 0)
		return RString(averr_ssprintf(ret, "Couldn't open codec \"%s\"", pCodec->name));
	ASSERT(av_stream_codec_->codec != nullptr);

	return RString();
}

void MovieDecoder_FFMpeg::Close()
{
	if (av_stream_ && av_stream_codec_->codec)
	{
		avcodec::avcodec_close(av_stream_codec_);
		av_stream_ = nullptr;
	}

	if (av_format_context_)
	{
		avcodec::avformat_close_input(&av_format_context_);
		av_format_context_ = nullptr;
	}

	Init();
}

void MovieDecoder_FFMpeg::Rewind()
{
	// Frames aren't kept after they're shown, so looping means decoding the
	// movie again: drop the buffer and have the decode thread seek to the start.
	std::lock_guard<std::mutex> lk(buffer_mutex_);
	display_frame_num_ = 0;
	buffer_base_ = 0;
	frame_buffer_.clear();
	end_of_file_ = 0;
	++generation_;
	rewind_requested_ = true;
	buffer_cv_.notify_all();
}

RageSurface* MovieDecoder_FFMpeg::CreateCompatibleSurface(int iTextureWidth, int iTextureHeight, bool bPreferHighColor, MovieDecoderPixelFormatYCbCr& fmtout)
{
	return RageMovieTextureDriver_FFMpeg::AVCodecCreateCompatibleSurface(iTextureWidth, iTextureHeight, bPreferHighColor, *ConvertValue<int>(&av_pixel_format_), fmtout);
}

MovieTexture_FFMpeg::MovieTexture_FFMpeg(RageTextureID ID) :
	MovieTexture_Generic(ID, new MovieDecoder_FFMpeg)
{
}

RageMovieTexture* RageMovieTextureDriver_FFMpeg::Create(RageTextureID ID, RString& sError)
{
	MovieTexture_FFMpeg* pRet = new MovieTexture_FFMpeg(ID);
	sError = pRet->Init();
	if (!sError.empty())
		SAFE_DELETE(pRet);
	return pRet;
}

REGISTER_MOVIE_TEXTURE_CLASS(FFMpeg);

/*
 * (c) 2003-2005 Glenn Maynard
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
