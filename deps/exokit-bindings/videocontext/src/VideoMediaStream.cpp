#include "VideoMediaStream.h"

#include <defines.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <libavutil/time.h>

#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

namespace ffmpeg {

  std::vector<VideoMediaStream *> videoCaptures;

  struct VideoMediaStream_private {
    bool loaded;
    bool playing;
    bool loop;
    int64_t startTime;
    double startFrameTime;
    Nan::Persistent<Uint8ClampedArray> dataArray;
    bool dataDirty;

    AVCodecContext  *pCodecCtx;
    AVFormatContext *pFormatCtx;
    AVCodec* pCodec;
    AVInputFormat* iformat;
    int videoStream;
    AVFrame* pFrame;
    AVFrame* pFrameRGB;
    AVPacket packet;

    VideoMediaStream_private()
      : loaded(false), playing(false), loop(false), startTime(0), startFrameTime(0), dataDirty(false)
        , pCodecCtx(NULL), pFormatCtx(NULL), pCodec(NULL), iformat(NULL), videoStream(0), pFrame(NULL), pFrameRGB(NULL)
    {
    }

    ~VideoMediaStream_private()
    {
      av_free_packet(&packet);
      if (pFrame) {
        av_free(pFrame);
      }
      if (pFrameRGB) {
        av_free(pFrameRGB);
      }
    }
  };

  VideoMediaStream::VideoMediaStream()
    : impl(new VideoMediaStream_private)
  {
    videoCaptures.push_back(this);
  }

  VideoMediaStream::~VideoMediaStream()
  {
    videoCaptures.erase(std::find(videoCaptures.begin(), videoCaptures.end(), this));
    delete impl;
  }

  Handle<Object> VideoMediaStream::Initialize(Isolate *isolate) {

    Nan::EscapableHandleScope scope;

    // constructor
    Local<FunctionTemplate> ctor = Nan::New<FunctionTemplate>(New);
    ctor->InstanceTemplate()->SetInternalFieldCount(1);
    ctor->SetClassName(JS_STR("VideoMediaStream"));

    // prototype
    Local<ObjectTemplate> proto = ctor->PrototypeTemplate();
    Nan::SetMethod(proto, "update", Update);
    Nan::SetMethod(proto, "play", Play);
    Nan::SetMethod(proto, "pause", Pause);
    Nan::SetAccessor(proto, JS_STR("width"), WidthGetter);
    Nan::SetAccessor(proto, JS_STR("height"), HeightGetter);
    Nan::SetAccessor(proto, JS_STR("data"), DataGetter);

    Local<Function> ctorFn = ctor->GetFunction();

    ctorFn->Set(JS_STR("updateAll"), Nan::New<Function>(UpdateAll));

    // register all codecs/devices/filters
    av_register_all(); 
    avcodec_register_all();
    avdevice_register_all();
    //avfilter_register_all();

    return scope.Escape(ctorFn);
  }

  NAN_METHOD(VideoMediaStream::New) {
    Nan::HandleScope scope;

    VideoMediaStream *video = new VideoMediaStream();
    Local<Object> videoObj = info.This();
    video->Wrap(videoObj);

    info.GetReturnValue().Set(videoObj);
  }

  void VideoMediaStream::Update() {
    int res;
    int frameFinished;
    if(res = av_read_frame(impl->pFormatCtx,&impl->packet)>=0)
    {
      if(impl->packet.stream_index == impl->videoStream){

        avcodec_decode_video2(impl->pCodecCtx,impl->pFrame,&frameFinished,&impl->packet);

        if(frameFinished){

          struct SwsContext * img_convert_ctx;
          img_convert_ctx = sws_getCachedContext(NULL,impl->pCodecCtx->width, impl->pCodecCtx->height, impl->pCodecCtx->pix_fmt,   impl->pCodecCtx->width, impl->pCodecCtx->height, AV_PIX_FMT_RGB24, SWS_BICUBIC, NULL, NULL,NULL);
          sws_scale(img_convert_ctx, ((AVPicture*)impl->pFrame)->data, ((AVPicture*)impl->pFrame)->linesize, 0, impl->pCodecCtx->height, ((AVPicture *)impl->pFrameRGB)->data, ((AVPicture *)impl->pFrameRGB)->linesize);
          impl->dataDirty = true;

          /*
          //OpenCV
          cv::Mat img(impl->pFrame->height,impl->pFrame->width,CV_8UC3,impl->pFrameRGB->data[0]); 
          cv::imshow("display",img);
          cvWaitKey(1);
          */

          av_free_packet(&impl->packet);
          sws_freeContext(img_convert_ctx);

        }

      }

    }
  }

  void VideoMediaStream::Play() {
  }

  void VideoMediaStream::Pause() {
  }

  void VideoMediaStream::SeekTo(double timestamp) {
  }

  uint32_t VideoMediaStream::GetWidth() {
    return impl->pCodecCtx->width;
  }

  uint32_t VideoMediaStream::GetHeight() {
    return impl->pCodecCtx->height;
  }

  NAN_METHOD(VideoMediaStream::Update) {
    VideoMediaStream *video = ObjectWrap::Unwrap<VideoMediaStream>(info.This());
    video->Update();
  }

  NAN_METHOD(VideoMediaStream::Play) {
    VideoMediaStream *video = ObjectWrap::Unwrap<VideoMediaStream>(info.This());
    video->Play();
  }

  NAN_METHOD(VideoMediaStream::Pause) {
    VideoMediaStream *video = ObjectWrap::Unwrap<VideoMediaStream>(info.This());
    video->Pause();
  }

  NAN_GETTER(VideoMediaStream::WidthGetter) {
    Nan::HandleScope scope;

    VideoMediaStream *video = ObjectWrap::Unwrap<VideoMediaStream>(info.This());
    info.GetReturnValue().Set(JS_INT(video->GetWidth()));
  }

  NAN_GETTER(VideoMediaStream::HeightGetter) {
    Nan::HandleScope scope;

    VideoMediaStream *video = ObjectWrap::Unwrap<VideoMediaStream>(info.This());
    info.GetReturnValue().Set(JS_INT(video->GetHeight()));
  }

  NAN_GETTER(VideoMediaStream::DataGetter) {
    Nan::HandleScope scope;

    VideoMediaStream *video = ObjectWrap::Unwrap<VideoMediaStream>(info.This());

    unsigned int width = video->GetWidth();
    unsigned int height = video->GetHeight();
    unsigned int dataSize = width * height * 3;
    AVPixelFormat  pFormat = AV_PIX_FMT_RGB24;
    dataSize = avpicture_get_size(pFormat,width, height);

    Local<Uint8ClampedArray> uint8ClampedArray = Nan::New(video->impl->dataArray);
    if (video->impl->dataDirty) {
      Local<ArrayBuffer> arrayBuffer = uint8ClampedArray->Buffer();
      uint8_t* buffer = (uint8_t *)arrayBuffer->GetContents().Data() + uint8ClampedArray->ByteOffset();
      avpicture_fill((AVPicture *) video->impl->pFrame,buffer,pFormat,width, height);
      video->impl->dataDirty = false;
    }

    info.GetReturnValue().Set(uint8ClampedArray);
  }

  NAN_METHOD(VideoMediaStream::UpdateAll) {
    for (auto i : videoCaptures) {
      i->Update();
    }
  }

}
