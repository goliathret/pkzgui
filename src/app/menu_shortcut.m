#import <Cocoa/Cocoa.h>

void SetMenuShortcut(void* item, const char* key)
{
    NSMenuItem* menuItem = (NSMenuItem*)item;
    menuItem.keyEquivalent = [NSString stringWithUTF8String:key];
    menuItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
}
