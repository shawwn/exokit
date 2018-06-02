#ifndef _VIDEO_MEDIA_STREAM_H_
#define _VIDEO_MEDIA_STREAM_H_

#include "VideoCommon.h"
#include <v8.h>
#include <node.h>
#include <nan/nan.h>

using namespace std;
using namespace v8;
using namespace node;

namespace ffmpeg {

struct VideoMediaStream_private;

class VideoMediaStream : public ObjectWrap {
public:
  static Handle<Object> Initialize(Isolate *isolate);

  void Update();
  void Play();
  void Pause();
  void SeekTo(double timestamp);
  uint32_t GetWidth();
  uint32_t GetHeight();

protected:
  static NAN_METHOD(New);
  static NAN_METHOD(GetTracks);

  static NAN_METHOD(Update);
  static NAN_METHOD(Play);
  static NAN_METHOD(Pause);
  static NAN_GETTER(WidthGetter);
  static NAN_GETTER(HeightGetter);
  static NAN_GETTER(DataGetter);
  static NAN_METHOD(UpdateAll);

  VideoMediaStream();
  ~VideoMediaStream();

private:
  VideoMediaStream_private* impl;
};

}

#endif

