#include "macpermissions.h"
#import <AVFoundation/AVFoundation.h>
QString deskPortMicrophoneStatus() {
    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]) {
    case AVAuthorizationStatusAuthorized: return QStringLiteral("allowed");
    case AVAuthorizationStatusDenied: return QStringLiteral("denied");
    case AVAuthorizationStatusRestricted: return QStringLiteral("restricted");
    default: return QStringLiteral("notRequested");
    }
}
