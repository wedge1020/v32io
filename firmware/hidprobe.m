// hidprobe.m - does macOS hand the v32io pad to an ordinary IOKit HID client?
//
//   clang -fobjc-arc hidprobe.m -o hidprobe \
//         -framework IOKit -framework Foundation -framework GameController
//   ./hidprobe        (then move/click the mouse on the adapter for 15 s)
//
// Matches every HID device with Generic Desktop / Game Pad usage and prints
// what an app like SDL would see for it. Not run by its author (Linux box).

#import <Foundation/Foundation.h>
#import <IOKit/hid/IOHIDLib.h>
#import <GameController/GameController.h>

static long num( IOHIDDeviceRef d, CFStringRef key )
{
    CFTypeRef p = IOHIDDeviceGetProperty( d, key );
    long n = 0;
    if( p && CFGetTypeID(p) == CFNumberGetTypeID() )
        CFNumberGetValue( (CFNumberRef) p, kCFNumberLongType, &n );
    return n;
}

static void onValue( void *ctx, IOReturn r, void *sender, IOHIDValueRef v )
{
    IOHIDElementRef e = IOHIDValueGetElement( v );
    printf( "    value: page 0x%x usage 0x%x = %ld\n",
            IOHIDElementGetUsagePage(e), IOHIDElementGetUsage(e),
            IOHIDValueGetIntegerValue(v) );
}

static void onAdd( void *ctx, IOReturn r, void *sender, IOHIDDeviceRef d )
{
    CFTypeRef name  = IOHIDDeviceGetProperty( d, CFSTR(kIOHIDProductKey) );
    CFTypeRef synth = IOHIDDeviceGetProperty( d, CFSTR("GCSyntheticDevice") );
    BOOL gc = [GCController supportsHIDDevice:d];

    printf( "HID pad/joystick: %s  vid 0x%lx pid 0x%lx  GC supports: %s  synthetic: %s\n",
            name? [(__bridge NSString*)name UTF8String] : "?",
            num(d, CFSTR(kIOHIDVendorIDKey)), num(d, CFSTR(kIOHIDProductIDKey)),
            gc? "YES" : "no", (synth == kCFBooleanTrue)? "YES" : "no" );

    if( num(d, CFSTR(kIOHIDVendorIDKey)) != 0xCAFE )
        return;                                     // only open ours

    IOReturn o = IOHIDDeviceOpen( d, kIOHIDOptionsTypeNone );
    printf( "  IOHIDDeviceOpen: 0x%08x %s\n", o, (o == kIOReturnSuccess)? "(ok)" : "(FAILED)" );

    CFArrayRef els = IOHIDDeviceCopyMatchingElements( d, NULL, kIOHIDOptionsTypeNone );
    printf( "  elements: %ld\n", els? (long) CFArrayGetCount(els) : -1L );
    if( els ) CFRelease( els );

    IOHIDDeviceRegisterInputValueCallback( d, onValue, NULL );
    IOHIDDeviceScheduleWithRunLoop( d, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode );
}

int main( void )
{
    @autoreleasepool
    {
        IOHIDManagerRef m = IOHIDManagerCreate( kCFAllocatorDefault, kIOHIDOptionsTypeNone );
        NSArray *match = @[
          @{ @kIOHIDDeviceUsagePageKey: @(kHIDPage_GenericDesktop), @kIOHIDDeviceUsageKey: @(kHIDUsage_GD_GamePad) },
          @{ @kIOHIDDeviceUsagePageKey: @(kHIDPage_GenericDesktop), @kIOHIDDeviceUsageKey: @(kHIDUsage_GD_Joystick) } ];
        IOHIDManagerSetDeviceMatchingMultiple( m, (__bridge CFArrayRef) match );
        IOHIDManagerRegisterDeviceMatchingCallback( m, onAdd, NULL );
        IOHIDManagerScheduleWithRunLoop( m, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode );
        IOReturn r = IOHIDManagerOpen( m, kIOHIDOptionsTypeNone );
        printf( "IOHIDManagerOpen: 0x%08x\n", r );

        CFRunLoopRunInMode( kCFRunLoopDefaultMode, 15, false );

        NSArray *cs = [GCController controllers];
        printf( "GCController.controllers: %lu\n", (unsigned long) cs.count );
    }
    return 0;
}
