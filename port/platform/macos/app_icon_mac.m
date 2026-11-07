/*
 * port/platform/macos/app_icon_mac.m
 *
 * The Dock icon of the running program (app_icon_window.h).  SDL's Cocoa
 * windows have no icon; the bundle's own Finder icon stays the generic one
 * (no CFBundleIconFile: the bundle is signed, and the icon is made from the
 * player's disc, so it is never shipped).  Main thread only.
 */
#import <AppKit/AppKit.h>

#include "../app_icon_window.h"

int ico_app_icon_mac_set(const unsigned char *png, unsigned long n)
{
    if (png == NULL || n == 0 || NSApp == nil) {
        return 0;
    }
    @autoreleasepool {
        NSData *data = [NSData dataWithBytes:png length:(NSUInteger)n];
        NSImage *img = [[NSImage alloc] initWithData:data];
        if (img == nil) {
            return 0;
        }
        [NSApp setApplicationIconImage:img];
    }
    return 1;
}
