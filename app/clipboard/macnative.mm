#include "native.h"
#include <QUrl>
#import <AppKit/AppKit.h>

namespace {
NSString* nativeType(const QString& mime) {
    // AppKit rejects slash-containing MIME names as provider UTIs. Keep this
    // local marker reversible so the agent can suppress echoes and revoke it.
    if (mime == "application/x-deskport-clipboard") return @"io.github.keithxc.deskport.clipboard-marker";
    if (mime == "text/plain" || mime == "text/plain;charset=utf-8") return NSPasteboardTypeString;
    if (mime == "image/png") return NSPasteboardTypePNG;
    if (mime == "image/tiff") return NSPasteboardTypeTIFF;
    if (mime == "text/uri-list") return NSPasteboardTypeFileURL;
    return [NSString stringWithUTF8String:mime.toUtf8().constData()];
}
QString mimeType(NSString* type) {
    if ([type isEqualToString:@"io.github.keithxc.deskport.clipboard-marker"]) return "application/x-deskport-clipboard";
    if ([type isEqualToString:NSPasteboardTypeString]) return "text/plain;charset=utf-8";
    if ([type isEqualToString:NSPasteboardTypePNG]) return "image/png";
    if ([type isEqualToString:NSPasteboardTypeTIFF]) return "image/tiff";
    if ([type isEqualToString:@"NSFilenamesPboardType"] || [type isEqualToString:NSPasteboardTypeFileURL]) return "text/uri-list";
    return QString::fromUtf8([type UTF8String]);
}
}
@interface DeskPortPasteboardProvider : NSObject <NSPasteboardItemDataProvider> {
@public
    ClipboardNative::Reader reader;
    int index;
}
@end
@implementation DeskPortPasteboardProvider
- (void)pasteboard:(NSPasteboard*)board item:(NSPasteboardItem*)item provideDataForType:(NSPasteboardType)type {
    Q_UNUSED(board);
    // Copy the callback because a new remote offer can arrive during a download.
    const auto callback = reader;
    if (!callback) return;
    auto bytes = callback(mimeType(type));
    if ([type isEqualToString:NSPasteboardTypeFileURL]) bytes = bytes.split('\n').value(index).trimmed();
    [item setData:[NSData dataWithBytes:bytes.constData() length:bytes.size()] forType:type];
}
@end
namespace {
class MacClipboard final : public ClipboardNative {
    NSPasteboard* board;
    NSMutableArray* providers;
public:
    explicit MacClipboard(const QString& name) {
        board = [(name.isEmpty() ? [NSPasteboard generalPasteboard] : [NSPasteboard pasteboardWithName:[NSString stringWithUTF8String:name.toUtf8().constData()]]) retain];
        providers = [[NSMutableArray alloc] init];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
    }
    ~MacClipboard() override {
        for (DeskPortPasteboardProvider* provider in providers) provider->reader = {};
        [providers release]; [board release];
    }
    qint64 revision() override { @autoreleasepool { return board.changeCount; } }
    QStringList formats() override {
        @autoreleasepool { QStringList result; for (NSString* type in board.types) result << mimeType(type); if (result.contains("image/tiff") && !result.contains("image/png")) result << "image/png"; result.removeDuplicates(); return result; }
    }
    QByteArray read(const QString& mime) override {
        @autoreleasepool {
            if (mime == "text/uri-list") {
                QByteArray result;
                for (NSPasteboardItem* item in board.pasteboardItems) {
                    NSString* url = [item stringForType:NSPasteboardTypeFileURL];
                    if (url) result += QByteArray([url UTF8String]) + "\r\n";
                }
                if (result.isEmpty()) {
                    id paths = [board propertyListForType:@"NSFilenamesPboardType"];
                    if ([paths isKindOfClass:[NSArray class]]) for (id path in paths)
                        if ([path isKindOfClass:[NSString class]]) result += QUrl::fromLocalFile(QString::fromUtf8([path UTF8String])).toEncoded() + "\r\n";
                }
                return result;
            }
            NSData* data = [board dataForType:nativeType(mime)];
            if (!data && mime == "image/png") {
                NSData* tiff = [board dataForType:NSPasteboardTypeTIFF];
                if (tiff.length > 128ULL * 1024 * 1024) return {};
                NSBitmapImageRep* image = tiff ? [NSBitmapImageRep imageRepWithData:tiff] : nil;
                if (!image || image.pixelsWide * image.pixelsHigh > 32LL * 1024 * 1024) return {};
                data = [image representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
            }
            if (!data || data.length > 128ULL * 1024 * 1024) return {};
            return QByteArray(static_cast<const char*>(data.bytes), int(data.length));
        }
    }
    void publish(const QStringList& formats, Reader reader, int count = 1) override {
        @autoreleasepool {
            NSMutableArray* items = [NSMutableArray array];
            NSMutableArray* types = [NSMutableArray array];
            for (const auto& mime : formats) { NSString* type = nativeType(mime); if (![types containsObject:type]) [types addObject:type]; }
            for (int i = 0; i < count; ++i) {
                auto provider = [[DeskPortPasteboardProvider alloc] init]; provider->reader = reader; provider->index = i;
                auto item = [[NSPasteboardItem alloc] init];
                [item setDataProvider:provider forTypes:types]; [items addObject:item]; [providers addObject:provider];
                [item release]; [provider release];
            }
            [board clearContents]; [board writeObjects:items];
            // Providers can be retained by pasteboard callbacks while a nested
            // request runs. Keep only current providers after writeObjects.
            while (providers.count > static_cast<NSUInteger>(count)) {
                DeskPortPasteboardProvider* old = [providers objectAtIndex:0]; old->reader = {}; [providers removeObjectAtIndex:0];
            }
        }
    }
};
}
std::unique_ptr<ClipboardNative> makeMacClipboardNative(const QString& board) { return std::unique_ptr<ClipboardNative>(new MacClipboard(board)); }
