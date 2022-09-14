#include <SpeechEcallController.h>
#include <SpeechDriverNormal.h>

#include <AudioAssert.h>//Mutex/assert
#include <AudioLock.h>
#include <AudioEventThreadManager.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "SpeechEcallController"
namespace android {

/*
 * =============================================================================
 *                     typedef
 * =============================================================================
 */
#define ECALL_IPC_FOR_UPLINK "/tmp/ecall/ecall_flow_recv_from_lower"
#define ECALL_IPC_FOR_INDICATION "/tmp/ecall/ecall_flow_report_indication"

/*
 * =============================================================================
 *                     Callback
 * =============================================================================
 */
static void callbackEcallIndication(int audioEventType, void *caller, void *arg) {

    SpeechDriverNormal *speechDriver = NULL;
    speechDriver = static_cast<SpeechDriverNormal *>(arg);

    ALOGD("%s(), audioEventType = %d, notifier(%p)",
          __FUNCTION__, audioEventType, arg);

    int retval = 0;
    int uplinkfd = 0;
    int retry = 0;
    int bytesWritten = 0;
    void *bufStart = NULL;
    int bytesLeft = 0;
    spcEcallIndicationStruct ecallIndication = speechDriver->mEcallIndication;

    if (speechDriver == NULL) {
        ALOGE("%s(), NULL!! speechDriver %p", __FUNCTION__, speechDriver);
        return;
    }
    ALOGD("%s(), ecallIndication.header = %d, ecallIndication.data = %d",
          __FUNCTION__, ecallIndication.header, ecallIndication.data);

    uplinkfd = open(ECALL_IPC_FOR_INDICATION , O_WRONLY);
    
    if (uplinkfd >= 0) {
        bufStart = &ecallIndication;
        bytesLeft = sizeof(uint32_t)*2;
        ALOGV("%s(), uplinkfd:0x%x, bufStart:0x%x, bytesLeft=%d", __FUNCTION__, uplinkfd, bufStart, bytesLeft);

        while ((bytesLeft != 0) && (retry < 10)) {
            bytesWritten = write(uplinkfd, bufStart, bytesLeft);
            if((-1 == bytesWritten) && (EAGAIN != errno)) {
                ALOGE("%s(), Write to server FIFO failed: errno(%d), bytes_write(%d)",
                      __FUNCTION__, errno, bytesWritten);
                close(uplinkfd);
            }
            bufStart += bytesWritten;
            bytesLeft -= bytesWritten;
            usleep(10 * 1000);
            retry ++;
        }

        close(uplinkfd);
        /* send read ack to modem */
        speechDriver->eCallInfo();
    } else {
        ALOGE("%s(), open %s Fail!!! uplinkfd:0x%x", __FUNCTION__, ECALL_IPC_FOR_INDICATION);
    }
}

static void callbackEcallRx(int audioEventType, void *caller, void *arg) {

    SpeechDriverNormal *speechDriver = NULL;
    speechDriver = static_cast<SpeechDriverNormal *>(arg);

    ALOGD("%s(), audioEventType = %d, notifier(%p)",
          __FUNCTION__, audioEventType, arg);

    int retval = 0;
    int uplinkfd;
    int retry = 0;
    int bytesWritten = 0;
    void *bufStart = NULL;
    int bytesLeft = 0;

    if (speechDriver == NULL) {
        ALOGE("%s(), NULL!! speechDriver %p", __FUNCTION__, speechDriver);
        return;
    }

    uplinkfd = open(ECALL_IPC_FOR_UPLINK , O_WRONLY);
    if (uplinkfd >= 0) {
        bufStart = speechDriver->mEcallRXCtrlData.data;
        bytesLeft = speechDriver->mEcallRXCtrlData.size;
        ALOGV("%s(), uplinkfd:0x%x, bufStart:0x%x, bytesLeft=%d", __FUNCTION__, uplinkfd, bufStart, bytesLeft);

        while ((bytesLeft != 0) && (retry < 10)) {
            bytesWritten = write(uplinkfd, bufStart, bytesLeft);
            if((-1 == bytesWritten) && (EAGAIN != errno)) {
                ALOGE("%s(), Write to server FIFO failed: errno(%d), bytesWritten(%d)",
                      __FUNCTION__, errno, bytesWritten);
                close(uplinkfd);
            }
            bufStart += bytesWritten;
            bytesLeft -= bytesWritten;
            usleep(10 * 1000);
            retry ++;
        }

        close(uplinkfd);
        /* send read ack to modem */
        speechDriver->eCallRxCtrl();
    } else {
        ALOGE("%s(), open %s Fail!!! uplinkfd:0x%x", __FUNCTION__, ECALL_IPC_FOR_INDICATION);
    }
}

/*
* =============================================================================
*                     class implementation
* =============================================================================
*/
SpeechEcallController::SpeechEcallController() {
    ALOGD("%s()", __FUNCTION__);
    AudioEventThreadManager::getInstance()->registerCallback(AUDIO_EVENT_ECALL_INDICATION,
                                                             callbackEcallIndication, this);
    AudioEventThreadManager::getInstance()->registerCallback(AUDIO_EVENT_ECALL_RX, callbackEcallRx, this);

}

SpeechEcallController::~SpeechEcallController() {
    ALOGD("%s()", __FUNCTION__);

}

} /* end of namespace android */

