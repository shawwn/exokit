#ifndef _HTML_VIDEO_CAPTURE_ELEMENT_H_
#define _HTML_VIDEO_CAPTURE_ELEMENT_H_

#include <v8.h>
#include <node.h>
#include <nan/nan.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <libavutil/time.h>

#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
//#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
//#include <libavfilter/buffersink.h>
//#include <libavfilter/buffersrc.h>
}

#include <defines.h>

using namespace std;
using namespace v8;
using namespace node;

namespace ffmpeg {

class VideoCapture : public ObjectWrap {
public:
  static Handle<Object> Initialize(Isolate *isolate);
  bool Load(uint8_t *bufferValue, size_t bufferLength, string *error = nullptr);
  void Update();
  void Play();
  void Pause();
  void SeekTo(double timestamp);
  uint32_t GetWidth();
  uint32_t GetHeight();

protected:
  static NAN_METHOD(New);
  static NAN_METHOD(Load);
  static NAN_METHOD(Update);
  static NAN_METHOD(Play);
  static NAN_METHOD(Pause);
  static NAN_GETTER(WidthGetter);
  static NAN_GETTER(HeightGetter);
  static NAN_GETTER(DataGetter);
  static NAN_METHOD(UpdateAll);

  VideoCapture();
  ~VideoCapture();

  private:
    bool loaded;
    bool playing;
    bool loop;
    int64_t startTime;
    double startFrameTime;
    Nan::Persistent<Uint8ClampedArray> dataArray;
    bool dataDirty;


    AVCodecContext  *pCodecCtx;
    AVFormatContext *pFormatCtx;
    AVCodec * pCodec;
    AVInputFormat *iformat;
    int videoStream;
    AVFrame *pFrame, *pFrameRGB;
    AVPacket packet;
};

extern std::vector<VideoCapture *> videoCaptures;

}

#endif
