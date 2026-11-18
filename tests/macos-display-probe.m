// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only production topology snapshot. Never captures pixels or configures displays.
#import <Foundation/Foundation.h>
#import <ApplicationServices/ApplicationServices.h>
#import <AppKit/AppKit.h>
#import "../host/macos/session-topology.h"
int main(void) { @autoreleasepool {
    NSArray *topology=captureTopology(0);
    if (!topology) return 1;
    NSData *data=[NSJSONSerialization dataWithJSONObject:topology options:NSJSONWritingSortedKeys error:nil];
    if (!data) return 1;
    fwrite(data.bytes,1,data.length,stdout); putchar('\n');
} }
