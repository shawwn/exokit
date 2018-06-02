#include <VideoCapture.h>

#if defined(_WIN32)
#define EXO_OS_WIN 1
#elif defined(__APPLE__)
#define EXO_OS_OSX 1
#else
#define EXO_OS_LINUX 1
#define USING_V4L 1
#endif
#include <sstream>

extern "C" {
#include <libavutil/avstring.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
}

struct VideoMode;

  typedef std::string DeviceString;
  typedef std::pair<DeviceString, DeviceString> DevicePair;
  typedef std::vector<DevicePair> DeviceList;
  typedef std::vector<VideoMode> VideoModeList;

struct VideoMode
{
    int width, height;
    int x, y;
    float FPS = -1.0f;
    uint32_t pixel_format = 0;

    VideoMode(int width = 0, int height = 0, int x = 0, int y = 0, float FPS = -1.0f);

    operator bool() const;
    bool operator==(const VideoMode& other) const;
    uint32_t norm(const VideoMode& other) const;
    uint32_t tolerance() const;
    DeviceString desc() const {
      std::ostringstream stringStream;
      stringStream << width << "x" << height << " FPS=" << FPS << " pixel_format=" << pixel_format;
      return stringStream.str();
    }
};

VideoMode::VideoMode(int width, int height, int x, int y, float FPS)
    : width(width)
    , height(height)
    , x(x)
    , y(y)
    , FPS(FPS)
{
}

/*
VideoMode::VideoMode(QRect rect)
    : width(rect.width())
    , height(rect.height())
    , x(rect.x())
    , y(rect.y())
{
}

QRect VideoMode::toRect() const
{
    return QRect(x, y, width, height);
}
*/

template<typename T>
static bool FuzzyCompare(const T& a, const T& b) {
  return a == b;
}

static int Abs(int a) {
  if (a < 0) return -a;
  return a;
}

bool VideoMode::operator==(const VideoMode& other) const
{
    return width == other.width && height == other.height && x == other.x && y == other.y
           && FuzzyCompare(FPS, other.FPS) && pixel_format == other.pixel_format;
}

uint32_t VideoMode::norm(const VideoMode& other) const
{
    return Abs(this->width - other.width) + Abs(this->height - other.height);
}

uint32_t VideoMode::tolerance() const
{
    constexpr uint32_t minTolerance = 300; // keep wider tolerance for low res cameras
    constexpr uint32_t toleranceFactor = 10; // video mode must be within 10% to be "close enough" to ideal
    return std::max((width + height)/toleranceFactor, minTolerance);
}

/**
 * @brief All zeros means a default/unspecified mode
 */
VideoMode::operator bool() const
{
    return width || height || static_cast<int>(FPS);
}


#if EXO_OS_WIN
#pragma comment(lib, "Strmiids.lib");
namespace DirectShow {
DeviceList getDeviceList();
VideoModeList getDeviceModes(DeviceString devName);


}


// Because of replacing to incorrect order, which leads to building failing,
// this region is ignored for clang-format
// clang-format off
#include <cstdint>
#include <objbase.h>
#include <strmif.h>
#include <amvideo.h>
#include <dvdmedia.h>
#include <uuids.h>
#include <cassert>
// clang-format on

/**
 * Most of this file is adapted from libavdevice's dshow.c,
 * which retrieves useful information but only exposes it to
 * stdout and is not part of the public API for some reason.
 */

static char* wcharToUtf8(wchar_t* w)
{
    int l = WideCharToMultiByte(CP_UTF8, 0, w, -1, 0, 0, 0, 0);
    char* s = new char[l];
    if (s)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s, l, 0, 0);
    return s;
}

DeviceList DirectShow::getDeviceList()
{
    IMoniker* m = nullptr;
    DeviceList devices;

    ICreateDevEnum* devenum = nullptr;
    if (CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum,
                         (void**)&devenum)
        != S_OK) {
      printf("TKTK60\n");
        return devices;
    }

    IEnumMoniker* classenum = nullptr;
    if (devenum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, (IEnumMoniker**)&classenum, 0)
        != S_OK) {
      printf("TKTK61\n");
        return devices;
    }

    while (classenum->Next(1, &m, nullptr) == S_OK) {
        VARIANT var;
        IPropertyBag* bag = nullptr;
        LPMALLOC coMalloc = nullptr;
        IBindCtx* bindCtx = nullptr;
        LPOLESTR olestr = nullptr;
        char *devIdString = nullptr, *devHumanName = nullptr;

        if (CoGetMalloc(1, &coMalloc) != S_OK)
            goto fail;
        if (CreateBindCtx(0, &bindCtx) != S_OK)
            goto fail;

        // Get an uuid for the device that we can pass to ffmpeg directly
        if (m->GetDisplayName(bindCtx, nullptr, &olestr) != S_OK)
            goto fail;
        devIdString = wcharToUtf8(olestr);

        // replace ':' with '_' since FFmpeg uses : to delimitate sources
        for (size_t i = 0; i < strlen(devIdString); ++i)
            if (devIdString[i] == ':')
                devIdString[i] = '_';

        // Get a human friendly name/description
        if (m->BindToStorage(nullptr, nullptr, IID_IPropertyBag, (void**)&bag) != S_OK)
            goto fail;

        var.vt = VT_BSTR;
        if (bag->Read(L"FriendlyName", &var, nullptr) != S_OK)
            goto fail;
        devHumanName = wcharToUtf8(var.bstrVal);

        devices.push_back({DeviceString("video=") + devIdString, devHumanName});

    fail:
        if (olestr && coMalloc)
            coMalloc->Free(olestr);
        if (bindCtx)
            bindCtx->Release();
        delete[] devIdString;
        delete[] devHumanName;
        if (bag)
            bag->Release();
        m->Release();
    }
    classenum->Release();

    printf("TKTK62\n");
    return devices;
}

// Used (by getDeviceModes) to select a device
// so we can list its properties
static IBaseFilter* getDevFilter(DeviceString devName)
{
    IBaseFilter* devFilter = nullptr;
    devName = devName.substr(6); // Remove the "video="
    IMoniker* m = nullptr;

    ICreateDevEnum* devenum = nullptr;
    if (CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum,
                         (void**)&devenum)
        != S_OK)
        return devFilter;

    IEnumMoniker* classenum = nullptr;
    if (devenum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, (IEnumMoniker**)&classenum, 0)
        != S_OK)
        return devFilter;

    while (classenum->Next(1, &m, nullptr) == S_OK) {
        LPMALLOC coMalloc = nullptr;
        IBindCtx* bindCtx = nullptr;
        LPOLESTR olestr = nullptr;
        char* devIdString;

        if (CoGetMalloc(1, &coMalloc) != S_OK)
            goto fail;
        if (CreateBindCtx(0, &bindCtx) != S_OK)
            goto fail;

        if (m->GetDisplayName(bindCtx, nullptr, &olestr) != S_OK)
            goto fail;
        devIdString = wcharToUtf8(olestr);

        // replace ':' with '_' since FFmpeg uses : to delimitate sources
        for (size_t i = 0; i < strlen(devIdString); ++i)
            if (devIdString[i] == ':')
                devIdString[i] = '_';

        if (devName != devIdString)
            goto fail;

        if (m->BindToObject(0, 0, IID_IBaseFilter, (void**)&devFilter) != S_OK)
            goto fail;

    fail:
        if (olestr && coMalloc)
            coMalloc->Free(olestr);
        if (bindCtx)
            bindCtx->Release();
        delete[] devIdString;
        m->Release();
    }
    classenum->Release();

    if (!devFilter)
        fprintf(stderr, "Could't find the device %s\n", devName.c_str());

    return devFilter;
}

VideoModeList DirectShow::getDeviceModes(DeviceString devName)
{
    VideoModeList modes;

    IBaseFilter* devFilter = getDevFilter(devName);
    if (!devFilter)
        return modes;

    // The outter loop tries to find a valid output pin
    GUID category;
    DWORD r2;
    IEnumPins* pins = nullptr;
    IPin* pin;
    if (devFilter->EnumPins(&pins) != S_OK)
        return modes;

    while (pins->Next(1, &pin, nullptr) == S_OK) {
        IKsPropertySet* p = nullptr;
        PIN_INFO info;

        pin->QueryPinInfo(&info);
        info.pFilter->Release();
        if (info.dir != PINDIR_OUTPUT)
            goto next;
        if (pin->QueryInterface(IID_IKsPropertySet, (void**)&p) != S_OK)
            goto next;
        if (p->Get(AMPROPSETID_Pin, AMPROPERTY_PIN_CATEGORY, nullptr, 0, &category, sizeof(GUID), &r2)
            != S_OK)
            goto next;
        if (!IsEqualGUID(category, PIN_CATEGORY_CAPTURE))
            goto next;

        // Now we can list the video modes for the current pin
        // Prepare for another wall of spaghetti DIRECT SHOW QUALITY code
        {
            IAMStreamConfig* config = nullptr;
            VIDEO_STREAM_CONFIG_CAPS* vcaps = nullptr;
            int size, n;
            if (pin->QueryInterface(IID_IAMStreamConfig, (void**)&config) != S_OK)
                goto next;
            if (config->GetNumberOfCapabilities(&n, &size) != S_OK)
                goto pinend;

            assert(size == sizeof(VIDEO_STREAM_CONFIG_CAPS));
            vcaps = new VIDEO_STREAM_CONFIG_CAPS;

            for (int i = 0; i < n; ++i) {
                AM_MEDIA_TYPE* type = nullptr;
                VideoMode mode;
                if (config->GetStreamCaps(i, &type, (BYTE*)vcaps) != S_OK)
                    goto nextformat;

                if (!IsEqualGUID(type->formattype, FORMAT_VideoInfo)
                    && !IsEqualGUID(type->formattype, FORMAT_VideoInfo2))
                    goto nextformat;

                mode.width = vcaps->MaxOutputSize.cx;
                mode.height = vcaps->MaxOutputSize.cy;
                mode.FPS = 1e7 / vcaps->MinFrameInterval;
                if (std::find(modes.begin(), modes.end(), mode) == modes.end())
                    modes.push_back(std::move(mode));

            nextformat:
                if (type->pbFormat)
                    CoTaskMemFree(type->pbFormat);
                CoTaskMemFree(type);
            }
        pinend:
            config->Release();
            delete vcaps;
        }
    next:
        if (p)
            p->Release();
        pin->Release();
    }

    return modes;
}


#endif

using namespace v8;

namespace ffmpeg {

  AVInputFormat *iformat;
  AVInputFormat *idesktopFormat;

bool getDefaultInputFormat()
{
    if (iformat)
        return true;

    //avdevice_register_all();

// Desktop capture input formats
#if USING_V4L
    idesktopFormat = av_find_input_format("x11grab");
#endif
#ifdef EXO_OS_WIN
    idesktopFormat = av_find_input_format("gdigrab");
#endif

// Webcam input formats
#if USING_V4L
    if ((iformat = av_find_input_format("v4l2")))
        return true;
#endif

#ifdef EXO_OS_WIN
    if ((iformat = av_find_input_format("dshow")))
        return true;
    if ((iformat = av_find_input_format("vfwcap")))
        return true;
#endif

#ifdef EXO_OS_OSX
    if ((iformat = av_find_input_format("avfoundation")))
        return true;
    if ((iformat = av_find_input_format("qtkit")))
        return true;
#endif

    fprintf(stderr, "No valid input format found\n");
    return false;
}

static int print_device_sources(AVInputFormat *fmt, AVDictionary *opts)
{
    int ret, i;
    AVDeviceInfoList *device_list = NULL;

    if (!fmt || !fmt->priv_class  || !AV_IS_INPUT_DEVICE(fmt->priv_class->category))
        return AVERROR(EINVAL);

    printf("Auto-detected sources for %s:\n", fmt->name);
    if (!fmt->get_device_list) {
        ret = AVERROR(ENOSYS);
        printf("Cannot list sources. Not implemented.\n");
        goto fail;
    }

    if ((ret = avdevice_list_input_sources(fmt, NULL, opts, &device_list)) < 0) {
        printf("Cannot list sources.\n");
        goto fail;
    }

    for (i = 0; i < device_list->nb_devices; i++) {
        printf("%s %s [%s]\n", device_list->default_device == i ? "*" : " ",
               device_list->devices[i]->device_name, device_list->devices[i]->device_description);
    }

  fail:
    avdevice_free_list_devices(&device_list);
    return ret;
}

int show_sources(const char *arg)
{
    AVInputFormat *fmt = NULL;
    char *dev = NULL;
    AVDictionary *opts = NULL;
    int ret = 0;
    int error_level = av_log_get_level();

    av_log_set_level(AV_LOG_ERROR);

    /*
    if ((ret = show_sinks_sources_parse_arg(arg, &dev, &opts)) < 0)
        goto fail;
        */

    do {
        fmt = av_input_audio_device_next(fmt);
        if (fmt) {
            if (!strcmp(fmt->name, "lavfi"))
                continue; //it's pointless to probe lavfi
            if (dev && !av_match_name(dev, fmt->name))
                continue;
            print_device_sources(fmt, opts);
        }
    } while (fmt);
    do {
        fmt = av_input_video_device_next(fmt);
        if (fmt) {
            if (dev && !av_match_name(dev, fmt->name))
                continue;
            print_device_sources(fmt, opts);
        }
    } while (fmt);
  fail:
    av_dict_free(&opts);
    av_free(dev);
    av_log_set_level(error_level);
    return ret;
}


DeviceList getRawDeviceListGeneric()
{
    DeviceList devices;

    if (!getDefaultInputFormat()) {
      printf("TKTK42\n");
        return devices;
    }

    // Alloc an input device context
    AVFormatContext* s;
    if (!(s = avformat_alloc_context())) {
      printf("TKTK43\n");
        return devices;
    }

    if (!iformat->priv_class || !AV_IS_INPUT_DEVICE(iformat->priv_class->category)) {
      printf("TKTK44\n");
        avformat_free_context(s);
        return devices;
    }

    s->iformat = iformat;
    if (s->iformat->priv_data_size > 0) {
        s->priv_data = av_mallocz(s->iformat->priv_data_size);
        if (!s->priv_data) {
          printf("TKTK45\n");
            avformat_free_context(s);
            return devices;
        }
        if (s->iformat->priv_class) {
          printf("TKTK46\n");
            *(const AVClass**)s->priv_data = s->iformat->priv_class;
            av_opt_set_defaults(s->priv_data);
        }
    } else {
        s->priv_data = NULL;
    }

    // List the devices for this context
    AVDeviceInfoList* devlist = nullptr;
    AVDictionary* tmp = nullptr;
    av_dict_copy(&tmp, nullptr, 0);
    if (av_opt_set_dict2(s, &tmp, AV_OPT_SEARCH_CHILDREN) < 0) {
      printf("TKTK47\n");
        av_dict_free(&tmp);
        avformat_free_context(s);
        return devices;
    }
    int ret = avdevice_list_devices(s, &devlist);
    av_dict_free(&tmp);
    avformat_free_context(s);
    if (!devlist) {
      printf("TKTK48\n");
        fprintf(stderr, "avdevice_list_devices failed with %d (%d, %d)\n", ret, AVERROR(ENOSYS), AVERROR(ENOMEM));
        return devices;
    }

    // Convert the list to a QVector
    devices.resize(devlist->nb_devices);
    for (int i = 0; i < devlist->nb_devices; ++i) {
        AVDeviceInfo* dev = devlist->devices[i];
        devices[i].first = dev->device_name;
        devices[i].second = dev->device_description;
      printf("TKTK49 %s %s\n", dev->device_name, dev->device_description);
    }
    avdevice_free_list_devices(&devlist);
    return devices;
}

DeviceList getDeviceList()
{
  //show_sources(NULL);
    DeviceList devices;

    devices.push_back({"none", "No camera device set"});

    if (!getDefaultInputFormat()) {
      printf("TKTK50\n");
        return devices;
    }

    if (!iformat)
        ;
#ifdef EXO_OS_WIN
    else if (iformat->name == DeviceString("dshow")) {
      printf("TKTK51\n");
        devices = DirectShow::getDeviceList();
    }
#endif
#if USING_V4L
    //else if (iformat->name == DeviceString("video4linux2,v4l2"))
        //devices += v4l2::getDeviceList();
#endif
#ifdef EXO_OS_OSX
    //else if (iformat->name == DeviceString("avfoundation"))
        //devices += avfoundation::getDeviceList();
#endif
    else {
      /*
      DeviceList devlist = getRawDeviceListGeneric();
      for (const DevicePair& device : devlist)
        devices.push_back(device);
        */
      devices = getRawDeviceListGeneric();
    }

    printf("TKTK53 %d\n", (int)devices.size());
    for (const DevicePair& device : devices) {
      printf("TKTK52\n");
      printf("device %s %s\n", device.first.c_str(), device.second.c_str());
      const VideoModeList& modes = DirectShow::getDeviceModes(device.first);
      for (const VideoMode& mode : modes) {
        printf("  %s\n", mode.desc().c_str() );
      }
    }

    /*
    if (idesktopFormat) {
        if (idesktopFormat->name == DeviceString("x11grab")) {
            DeviceString dev = "x11grab#";
            const char* display = getenv("DISPLAY");

            if (display == NULL)
                dev += ":0";
            else
                dev += display;

            devices.push_back(DevicePair{
                dev, "Desktop as a camera input for screen sharing"});
        }
        if (idesktopFormat->name == DeviceString("gdigrab"))
            devices.push_back(DevicePair{
                "gdigrab#desktop",
                "Desktop as a camera input for screen sharing"});
    }
    */

    return devices;
}

DeviceString getDefaultDeviceName()
{
    //DeviceString defaultdev = Settings::getInstance().getVideoDev();
    DeviceString defaultdev = "invalid";

    if (!getDefaultInputFormat())
        return defaultdev;

    DeviceList devlist(getDeviceList());
    for (const DevicePair& device : devlist)
        if (defaultdev == device.first)
            return defaultdev;

    if (devlist.size() <= 0)
        return defaultdev;

    return devlist[devlist.size() - 1].first;
}

VideoCapture::VideoCapture() : loaded(false), playing(false), loop(false), startTime(0), startFrameTime(0), dataDirty(false) {
  uint8_t *buffer;
 
  DeviceString defaultDevice(getDefaultDeviceName());
    //const char  *filenameSrc = "video=e2eSoft iVCam";
  DeviceString src("video=");
  src += defaultDevice;
    //printf("TKTK1\n");
 
    /*
    AVCodecContext  *pCodecCtx;
    AVFormatContext *pFormatCtx = avformat_alloc_context();
    AVCodec * pCodec;
    AVInputFormat *iformat = av_find_input_format("dshow");
    AVFrame *pFrame, *pFrameRGB;
    */
    pFormatCtx = avformat_alloc_context();
    //printf("TKTK2 %d\n", (int)pFormatCtx);
    iformat = av_find_input_format("dshow");
    //printf("TKTK3 %d\n", (int)iformat);

    AVDictionary *options = NULL;
    //av_dict_set(&options, "video_size", "640x480", 0);
    av_dict_set(&options, "pixel_format", "rgb24", 0);
 
    if(avformat_open_input(&pFormatCtx,src.c_str(),iformat,NULL) < 0) abort();
    //av_dict_free(&options);
    //printf("TKTK4\n");
    if(avformat_find_stream_info(pFormatCtx, NULL) < 0)   abort();
    //printf("TKTK5\n");
    av_dump_format(pFormatCtx, 0, src.c_str(), 0);
    //printf("TKTK6\n");
    videoStream = 0;
    for(int i=0; i < pFormatCtx->nb_streams; i++)
    {
        if(pFormatCtx->streams[i]->codec->coder_type==AVMEDIA_TYPE_VIDEO)
        {
          //printf("TKTK7 %d\n", i);
            videoStream = i;
            break;
        }
    }
    //printf("TKTK8\n");
 
    if(videoStream == -1) abort();
    //printf("TKTK9\n");
    pCodecCtx = pFormatCtx->streams[videoStream]->codec;
 
    pCodec =avcodec_find_decoder(pCodecCtx->codec_id);
    //printf("TKTK10\n");
    if(pCodec==NULL) abort();
    //printf("TKTK11\n");

 
  if(avcodec_open2(pCodecCtx,pCodec,NULL) < 0) abort();
  //printf("TKTK12\n");

  pFrame    = av_frame_alloc();
  pFrameRGB = av_frame_alloc();

  unsigned int width = GetWidth();
  unsigned int height = GetHeight();
  AVPixelFormat  pFormat = AV_PIX_FMT_RGB24;
  unsigned int dataSize = avpicture_get_size(pFormat,width, height);
  {
    Local<ArrayBuffer> arrayBuffer = ArrayBuffer::New(Isolate::GetCurrent(), dataSize);
    Local<Uint8ClampedArray> uint8ClampedArray = Uint8ClampedArray::New(arrayBuffer, 0, arrayBuffer->ByteLength());
    dataArray.Reset(uint8ClampedArray);


    //buffer = (uint8_t *) av_malloc(numBytes*sizeof(uint8_t));

    //Local<ArrayBuffer> arrayBuffer = uint8ClampedArray->Buffer();
    uint8_t* buffer = (uint8_t *)arrayBuffer->GetContents().Data() + uint8ClampedArray->ByteOffset();
    avpicture_fill((AVPicture *) pFrameRGB,buffer,pFormat,width, height);

    //memcpy((unsigned char *)arrayBuffer->GetContents().Data() + uint8ClampedArray->ByteOffset(), &video->pFrameRGB->data[0], dataSize);
    dataDirty = false;
  }


  videoCaptures.push_back(this);
}

VideoCapture::~VideoCapture() {
  videoCaptures.erase(std::find(videoCaptures.begin(), videoCaptures.end(), this));
 
  av_free_packet(&packet);
  //avcodec_close(pCodecCtx);
  av_free(pFrame);
  av_free(pFrameRGB);
  //avformat_close_input(&pFormatCtx);
}

Handle<Object> VideoCapture::Initialize(Isolate *isolate) {

  Nan::EscapableHandleScope scope;

  // constructor
  Local<FunctionTemplate> ctor = Nan::New<FunctionTemplate>(New);
  ctor->InstanceTemplate()->SetInternalFieldCount(1);
  ctor->SetClassName(JS_STR("VideoCapture"));

  // prototype
  Local<ObjectTemplate> proto = ctor->PrototypeTemplate();
  Nan::SetMethod(proto, "load", Load);
  Nan::SetMethod(proto, "update", Update);
  Nan::SetMethod(proto, "play", Play);
  Nan::SetMethod(proto, "pause", Pause);
  Nan::SetAccessor(proto, JS_STR("width"), WidthGetter);
  Nan::SetAccessor(proto, JS_STR("height"), HeightGetter);
  //Nan::SetAccessor(proto, JS_STR("loop"), LoopGetter, LoopSetter);
  Nan::SetAccessor(proto, JS_STR("data"), DataGetter);
  //Nan::SetAccessor(proto, JS_STR("currentTime"), CurrentTimeGetter, CurrentTimeSetter);
  //Nan::SetAccessor(proto, JS_STR("duration"), DurationGetter);

  Local<Function> ctorFn = ctor->GetFunction();

  ctorFn->Set(JS_STR("updateAll"), Nan::New<Function>(UpdateAll));

  /*
#if EXO_OS_WIN
  HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
#endif
*/

  // register all codecs/devices/filters
  av_register_all(); 
  avcodec_register_all();
  avdevice_register_all();
  //avfilter_register_all();

  // we will capture from a dshow device
  // @see https://trac.ffmpeg.org/wiki/DirectShow
  // @see http://www.ffmpeg.org/ffmpeg-devices.html#dshow
  //AVInputFormat *inFrmt= av_find_input_format("dshow");
  return scope.Escape(ctorFn);
}

NAN_METHOD(VideoCapture::New) {
  Nan::HandleScope scope;

  VideoCapture *video = new VideoCapture();
  Local<Object> videoObj = info.This();
  video->Wrap(videoObj);

  info.GetReturnValue().Set(videoObj);
}

bool VideoCapture::Load(unsigned char *bufferValue, size_t bufferLength, string *error) {
}

void VideoCapture::Update() {
  //printf("TKTK13\n");
    int res;
    int frameFinished;
    if(res = av_read_frame(pFormatCtx,&packet)>=0)
    {
      //printf("TKTK14\n");
        if(packet.stream_index == videoStream){
          //printf("TKTK15\n");
 
            avcodec_decode_video2(pCodecCtx,pFrame,&frameFinished,&packet);
 
            if(frameFinished){
              //printf("TKTK16\n");

                struct SwsContext * img_convert_ctx;
                img_convert_ctx = sws_getCachedContext(NULL,pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,   pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_RGB24, SWS_BICUBIC, NULL, NULL,NULL);
                sws_scale(img_convert_ctx, ((AVPicture*)pFrame)->data, ((AVPicture*)pFrame)->linesize, 0, pCodecCtx->height, ((AVPicture *)pFrameRGB)->data, ((AVPicture *)pFrameRGB)->linesize);
              dataDirty = true;
 
 
                /*
                //OpenCV
                cv::Mat img(pFrame->height,pFrame->width,CV_8UC3,pFrameRGB->data[0]); 
                cv::imshow("display",img);
                cvWaitKey(1);
                */
 
                av_free_packet(&packet);
                sws_freeContext(img_convert_ctx);
 
            }
 
        }
 
    }
}

void VideoCapture::Play() {
}

void VideoCapture::Pause() {
}

void VideoCapture::SeekTo(double timestamp) {
}

uint32_t VideoCapture::GetWidth() {
  return pCodecCtx->width;
}

uint32_t VideoCapture::GetHeight() {
  return pCodecCtx->height;
}

NAN_METHOD(VideoCapture::Load) {
  /*
  if (info[0]->IsArrayBuffer()) {
    VideoCapture *video = ObjectWrap::Unwrap<VideoCapture>(info.This());

    Local<ArrayBuffer> arrayBuffer = Local<ArrayBuffer>::Cast(info[0]);

    string error;
    if (video->Load((uint8_t *)arrayBuffer->GetContents().Data(), arrayBuffer->ByteLength(), &error)) {
      // nothing
    } else {
      Nan::ThrowError(error.c_str());
    }
  } else if (info[0]->IsTypedArray()) {
    VideoCapture *video = ObjectWrap::Unwrap<VideoCapture>(info.This());

    Local<ArrayBufferView> arrayBufferView = Local<ArrayBufferView>::Cast(info[0]);
    Local<ArrayBuffer> arrayBuffer = arrayBufferView->Buffer();

    string error;
    if (video->Load((unsigned char *)arrayBuffer->GetContents().Data() + arrayBufferView->ByteOffset(), arrayBufferView->ByteLength())) {
      // nothing
    } else {
      Nan::ThrowError(error.c_str());
    }
  } else {
    Nan::ThrowError("invalid arguments");
  }
  */
}

NAN_METHOD(VideoCapture::Update) {
  VideoCapture *video = ObjectWrap::Unwrap<VideoCapture>(info.This());
  video->Update();
}

NAN_METHOD(VideoCapture::Play) {
  VideoCapture *video = ObjectWrap::Unwrap<VideoCapture>(info.This());
  video->Play();
}

NAN_METHOD(VideoCapture::Pause) {
  VideoCapture *video = ObjectWrap::Unwrap<VideoCapture>(info.This());
  video->Pause();
}

NAN_GETTER(VideoCapture::WidthGetter) {
  Nan::HandleScope scope;

  VideoCapture *video = ObjectWrap::Unwrap<VideoCapture>(info.This());
  info.GetReturnValue().Set(JS_INT(video->GetWidth()));
}

NAN_GETTER(VideoCapture::HeightGetter) {
  Nan::HandleScope scope;

  VideoCapture *video = ObjectWrap::Unwrap<VideoCapture>(info.This());
  info.GetReturnValue().Set(JS_INT(video->GetHeight()));
}

NAN_GETTER(VideoCapture::DataGetter) {
  Nan::HandleScope scope;

  VideoCapture *video = ObjectWrap::Unwrap<VideoCapture>(info.This());

  unsigned int width = video->GetWidth();
  unsigned int height = video->GetHeight();
  unsigned int dataSize = width * height * 3;
  AVPixelFormat  pFormat = AV_PIX_FMT_RGB24;
  dataSize = avpicture_get_size(pFormat,width, height);

  Local<Uint8ClampedArray> uint8ClampedArray = Nan::New(video->dataArray);
  if (video->dataDirty) {
    Local<ArrayBuffer> arrayBuffer = uint8ClampedArray->Buffer();
    uint8_t* buffer = (uint8_t *)arrayBuffer->GetContents().Data() + uint8ClampedArray->ByteOffset();
    avpicture_fill((AVPicture *) video->pFrame,buffer,pFormat,width, height);
    video->dataDirty = false;
  }

  info.GetReturnValue().Set(uint8ClampedArray);
}

NAN_METHOD(VideoCapture::UpdateAll) {
  for (auto i : videoCaptures) {
    i->Update();
  }
}

std::vector<VideoCapture *> videoCaptures;

}
