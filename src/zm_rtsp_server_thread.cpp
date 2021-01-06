#include "zm_rtsp_server_thread.h"
#include "zm_rtsp_server_device_source.h"
#include "zm_rtsp_server_h264_device_source.h"
#include "zm_rtsp_server_unicast_server_media_subsession.h"
#include <StreamReplicator.hh>

#if HAVE_RTSP_SERVER
#define ZM_RTSP_SERVER_BASE 10000

RTSPServerThread::RTSPServerThread(Monitor *p_monitor) : 
  monitor(p_monitor),
  terminate(0)
{
  //unsigned short rtsp_over_http_port = 0;
  //const char *realm = "ZoneMinder";
  //unsigned int timeout = 65;
  OutPacketBuffer::maxSize = 100000;

  scheduler = BasicTaskScheduler::createNew();
  env = BasicUsageEnvironment::createNew(*scheduler);
  authDB = nullptr;
  //authDB = new UserAuthenticationDatabase("ZoneMinder");
  //authDB->addUserRecord("username1", "password1"); // replace these with real strings

  portNumBits rtspServerPortNum = ZM_RTSP_SERVER_BASE + monitor->Id();
  rtspServer = RTSPServer::createNew(*env, rtspServerPortNum, authDB);

  if ( rtspServer == nullptr ) {
    Error("Failed to create rtspServer at port %d", rtspServerPortNum);
    return;
  }
  Debug(1, "RTSP prefix is %s", rtspServer->rtspURLPrefix());
}  // end RTSPServerThread::RTSPServerThread

RTSPServerThread::~RTSPServerThread() {
  if ( rtspServer ) {
    Medium::close(rtspServer);
  } // end if rtsp_server
  env->reclaim();
  delete scheduler;
}

int RTSPServerThread::run() {
  Debug(1, "RTSPServerThread::run()");
  if ( rtspServer )
    env->taskScheduler().doEventLoop(&terminate); // does not return
  Debug(1, "RTSPServerThread::done()");
  return 0;
}  // end in RTSPServerThread::run()

void RTSPServerThread::stop() {
  Debug(1, "RTSPServerThread::stop()");
  terminate = 1;
}  // end RTSPServerThread::stop()

bool RTSPServerThread::stopped() const {
  return terminate ? true : false;
}  // end RTSPServerThread::stopped()

void RTSPServerThread::addStream() {
  if ( !rtspServer )
    return;

  int queueSize = 10;
  bool useThread = true;
  int outfd = 0;
  bool repeatConfig = true;

  StreamReplicator* videoReplicator = nullptr;
  std::string rtpFormat;

  // We don't know which format we can support at this time.
  // Do we make it configurable, or wait until PrimeCapture to determine what is available
  rtpFormat.assign(getRtpFormat(PIX_FMT_HEVC, false));
  Debug(1, "RTSP: format %s", rtpFormat.c_str());
  if ( rtpFormat.empty() ) {
    //LOG(ERROR) << "No Streaming format supported for device " << camera_name.c_str() << std::endl;
    Error("No streaming format");
    return;
  } else {
    //LOG(INFO) << "Create Source ... " << camera_name.c_str() << std::endl;
    //bool muxTS = (muxer != NULL);
    bool muxTS = false;
    
    FramedSource *source = nullptr;
    if ( rtpFormat == "video/H264" ) {
      source = H264_ZoneMinderDeviceSource::createNew(*env, monitor, outfd, queueSize, useThread, repeatConfig, muxTS);
#if 0
      if ( muxTS ) {
        muxer->addNewVideoSource(source, 5);
        source = muxer;
      }
#endif
    } else if ( rtpFormat == "video/H265" ) {
      source = H265_ZoneMinderDeviceSource::createNew(*env, monitor, outfd, queueSize, useThread, repeatConfig, muxTS);
#if 0
      if ( muxTS ) {
        muxer->addNewVideoSource(source, 6);
        source = muxer;
      }
#endif
    }
    if ( source == nullptr ) {
      //LOG(ERROR) << "Unable to create source for device " << camera_name.c_str() << std::endl;
      Error("Unable to create source");
    } else {
      videoReplicator = StreamReplicator::createNew(*env, source, false);
    }
  }

  // Create Unicast Session
  //std::list<ServerMediaSubsession*> subSessions;
  if ( videoReplicator ) {
    ServerMediaSession *sms = ServerMediaSession::createNew(*env, "streamname");
    sms->addSubsession(UnicastServerMediaSubsession::createNew(*env, videoReplicator, rtpFormat));
      rtspServer->addServerMediaSession(sms);
    //subSessions.push_back(subSession);
    //addSession(baseUrl, subSessions);
    char *url = rtspServer->rtspURL(sms);
    Debug(1, "url is %s", url);
    *env << "ZoneMinder Media Server at " << url << "\n";
    delete[] url;
  } else {
    ////LOG(ERROR) << "No videoReplicator" << std::endl;
  }
}  // end void addStream

int RTSPServerThread::addSession(
    const std::string & sessionName,
    const std::list<ServerMediaSubsession*> & subSession
    ) {
  int nbSubsession = 0;
  if ( subSession.empty() == false ) {
    UsageEnvironment& env(rtspServer->envir());
    ServerMediaSession* sms = ServerMediaSession::createNew(env, sessionName.c_str());
    if ( sms != nullptr ) {
      std::list<ServerMediaSubsession*>::const_iterator subIt;
      for ( subIt = subSession.begin(); subIt != subSession.end(); ++subIt ) {
        sms->addSubsession(*subIt);
        nbSubsession ++;
      }

      rtspServer->addServerMediaSession(sms);

      char* url = rtspServer->rtspURL(sms);
      if ( url != nullptr ) {
        Info("Play this stream using the URL %s", url);
        delete[] url;
        url = nullptr;
      }
    }  // end if sms
  }  // end if subSession
  return nbSubsession;
}

// -----------------------------------------
//    convert V4L2 pix format to RTP mime
// -----------------------------------------
std::string RTSPServerThread::getRtpFormat(int format, bool muxTS) {
  std::string rtpFormat;
  if (muxTS) {
    rtpFormat = "video/MP2T";
  } else {
    switch(format) {
      case PIX_FMT_HEVC : rtpFormat = "video/H265"; break;
      case PIX_FMT_H264 : rtpFormat = "video/H264"; break;
      //case PIX_FMT_MJPEG: rtpFormat = "video/JPEG"; break;
      //case PIX_FMT_JPEG : rtpFormat = "video/JPEG"; break;
      case PIX_FMT_VP8  : rtpFormat = "video/VP8" ; break;
      case PIX_FMT_VP9  : rtpFormat = "video/VP9" ; break;
    }
  }

  return rtpFormat;
}
#endif