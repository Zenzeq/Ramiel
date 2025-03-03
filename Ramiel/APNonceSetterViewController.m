//
//  APNonceSetterViewController.m
//  Ramiel
//
//  Created by Matthew Pierson on 4/03/21.
//  Copyright © 2021 moski. All rights reserved.
//

#import "APNonceSetterViewController.h"
#import "../Pods/SSZipArchive/SSZipArchive/SSZipArchive.h"
#import "Device.h"
#import "FirmwareKeys.h"
#import "IPSW.h"
#import "RamielView.h"
#include "kairos.h"

@implementation APNonceSetterViewController

NSString *generatorString;
NSString *apnonceextractPath;
IPSW *apnonceIPSW;
Device *apnonceDevice;
FirmwareKeys *apnonceKeys;
irecv_error_t apnonceerror = 0;
int apnoncecheckNum = 0;
int apnoncecon = 0;

- (void)viewDidLoad {
    [super viewDidLoad];
    self.preferredContentSize = NSMakeSize(self.view.frame.size.width, self.view.frame.size.height);
    apnonceIPSW = [[IPSW alloc] initIPSWID];
    apnonceKeys = [[FirmwareKeys alloc] initFirmwareKeysID];
    apnonceDevice = [RamielView getConnectedDeviceInfo];
    [apnonceDevice resetConnection];
    [apnonceDevice setIRECVDeviceInfo:[apnonceDevice getIRECVClient]];
    [apnonceDevice setModel:[NSString stringWithFormat:@"%s", apnonceDevice.getIRECVDevice->product_type]];
    [apnonceDevice setHardware_model:[NSString stringWithFormat:@"%s", apnonceDevice.getIRECVDevice->hardware_model]];
    [apnonceDevice setIRECVDeviceInfo:(apnonceDevice.getIRECVClient)];
    [apnonceDevice setCpid:[NSString stringWithFormat:@"0x%04x", apnonceDevice.getIRECVDeviceInfo.cpid]];
    if (apnonceDevice.getIRECVDeviceInfo.bdid > 9) {
        [apnonceDevice setBdid:[NSString stringWithFormat:@"0x%u", apnonceDevice.getIRECVDeviceInfo.bdid]];
    } else {
        [apnonceDevice setBdid:[NSString stringWithFormat:@"0x0%u", apnonceDevice.getIRECVDeviceInfo.bdid]];
    }
    [apnonceDevice setSrtg:[NSString stringWithFormat:@"%s", apnonceDevice.getIRECVDeviceInfo.srtg]];
    [apnonceDevice setSerial_string:[NSString stringWithFormat:@"%s", apnonceDevice.getIRECVDeviceInfo.serial_string]];
    [apnonceDevice setEcid:apnonceDevice.getIRECVDeviceInfo.ecid];
    [apnonceDevice setClosedState:0];
}

- (IBAction)setAPNonceButton:(NSButton *)sender {
    if ([self->_generatorEntry.stringValue isEqualToString:@""]) {
        generatorString = @"0x1111111111111111";
    } else {
        if ([self->_generatorEntry.stringValue containsString:@".shsh"] ||
            [self->_generatorEntry.stringValue containsString:@".shsh2"]) {
            generatorString = [[NSDictionary dictionaryWithContentsOfFile:self->_generatorEntry.stringValue]
                objectForKey:@"generator"];
            if (generatorString == nil) {
                NSAlert *apnonceError = [[NSAlert alloc] init];
                [apnonceError setMessageText:@"Error: SHSH file does not contain a generator..."];
                [apnonceError setInformativeText:@"You cannot use this SHSH file to set your devices generator."];
                apnonceError.window.titlebarAppearsTransparent = true;
                [apnonceError runModal];

                self->_generatorEntry.stringValue = @"";
                return;
            }
        } else if ([self->_generatorEntry.stringValue length] != 18 ||
                   ![self->_generatorEntry.stringValue containsString:@"0x"]) {
            NSAlert *apnonceError = [[NSAlert alloc] init];
            [apnonceError setMessageText:@"Error: Invalid Generator, please try again..."];
            apnonceError.window.titlebarAppearsTransparent = true;
            [apnonceError runModal];

            self->_generatorEntry.stringValue = @"";
            return;
        } else {
            generatorString = self->_generatorEntry.stringValue;
        }
    }
    [self->_generatorEntry setEnabled:FALSE];
    [self->_generatorEntry setHidden:TRUE];
    [self->_setNonceButton setEnabled:FALSE];
    [self->_setNonceButton setHidden:TRUE];

    [self->_prog setHidden:FALSE];
    [self->_label setStringValue:@"Checking if device is in PWNDFU mode..."];
    [self->_label setHidden:FALSE];

    if (!([[NSString stringWithFormat:@"%@", [apnonceDevice getSerial_string]] containsString:@"checkm8"] ||
          [[NSString stringWithFormat:@"%@", [apnonceDevice getSerial_string]] containsString:@"eclipsa"])) {
        [self->_label setStringValue:@"Device is not in PWNDFU mode..."];
        while (TRUE) {
            NSAlert *pwnNotice = [[NSAlert alloc] init];
            [pwnNotice setMessageText:[NSString stringWithFormat:@"Your %@ is not in PWNDFU mode. "
                                                                 @"Please enter it now.",
                                                                 [apnonceDevice getModel]]];
            [pwnNotice addButtonWithTitle:@"Run checkm8"];
            pwnNotice.window.titlebarAppearsTransparent = true;
            NSModalResponse choice = [pwnNotice runModal];
            if (choice == NSAlertFirstButtonReturn) {

                if ([apnonceDevice runCheckm8] == 0) {
                    [apnonceDevice setIRECVDeviceInfo:[apnonceDevice getIRECVClient]];
                    break;
                }
            }
        }
    }
    [self->_label setStringValue:@"Device is in PWNDFU mode..."];

    [self downloadiBXX];
    [self loadIPSW];
}

- (void)loadIPSW {

    dispatch_async(dispatch_get_main_queue(), ^{
        [self->_prog incrementBy:16.66];
        [self->_label setStringValue:@"Patching iBSS/iBEC..."];
    });
    const char *ibssPath =
        [[NSString stringWithFormat:@"%@/RamielFiles/ibss.raw", [[NSBundle mainBundle] resourcePath]] UTF8String];
    const char *ibssPwnPath =
        [[NSString stringWithFormat:@"%@/RamielFiles/ibss.pwn", [[NSBundle mainBundle] resourcePath]] UTF8String];
    const char *args = [@"-v" UTF8String];

    int ret;
    sleep(1);
    ret = patchIBXX((char *)ibssPath, (char *)ibssPwnPath, (char *)args, 0);

    if (ret != 0) {
        dispatch_queue_t mainQueue = dispatch_get_main_queue();
        dispatch_sync(mainQueue, ^{
            [RamielView errorHandler:
                @"Failed to patch iBSS":[NSString stringWithFormat:@"Kairos returned with: %i", ret]:@"N/A"];
            [self->_prog setHidden:TRUE];
            [self.view.window.contentViewController dismissViewController:self];
            [apnonceDevice teardown];
            [apnonceIPSW teardown];
            return;
        });
    } else {
        const char *ibecPath =
            [[NSString stringWithFormat:@"%@/RamielFiles/ibec.raw", [[NSBundle mainBundle] resourcePath]] UTF8String];
        const char *ibecPwnPath =
            [[NSString stringWithFormat:@"%@/RamielFiles/ibec.pwn", [[NSBundle mainBundle] resourcePath]] UTF8String];
        ret = patchIBXX((char *)ibecPath, (char *)ibecPwnPath, (char *)args, 0);
        if (ret != 0) {
            dispatch_queue_t mainQueue = dispatch_get_main_queue();
            dispatch_sync(mainQueue, ^{
                [RamielView errorHandler:
                    @"Failed to patch iBEC":[NSString stringWithFormat:@"Kairos returned with: %i", ret]:@"N/A"];
                [self->_prog setHidden:TRUE];
                [self.view.window.contentViewController dismissViewController:self];
                [apnonceDevice teardown];
                [apnonceIPSW teardown];
                return;
            });
        }
    }

    [RamielView img4toolCMD:[NSString stringWithFormat:@"-c %@/RamielFiles/ibss.%@.patched -t ibss "
                                                       @"%@/RamielFiles/ibss.pwn",
                                                       [[NSBundle mainBundle] resourcePath], [apnonceDevice getModel],
                                                       [[NSBundle mainBundle] resourcePath]]];

    [RamielView img4toolCMD:[NSString stringWithFormat:@"-c %@/RamielFiles/ibec.%@.patched -t ibec "
                                                       @"%@/RamielFiles/ibec.pwn",
                                                       [[NSBundle mainBundle] resourcePath], [apnonceDevice getModel],
                                                       [[NSBundle mainBundle] resourcePath]]];
    NSString *apnonceshshPath;
    NSMutableDictionary *ramielPrefs = [NSMutableDictionary
        dictionaryWithDictionary:[NSDictionary dictionaryWithContentsOfFile:
                                                   [NSString stringWithFormat:@"%@/com.moski.RamielSettings.plist",
                                                                              [[NSBundle mainBundle] resourcePath]]]];
    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    NSString *documentsDirectory = [paths objectAtIndex:0];
    if (![[ramielPrefs objectForKey:@"customSHSHPath"] containsString:@"N/A"]) {
        if ([RamielView debugCheck])
            NSLog(@"Using user-provided SHSH from: %@", [ramielPrefs objectForKey:@"customSHSHPath"]);
        apnonceshshPath = [ramielPrefs objectForKey:@"customSHSHPath"];
    } else if ([[NSFileManager defaultManager]
                   fileExistsAtPath:[NSString stringWithFormat:@"%@/Ramiel/shsh/%@.shsh", documentsDirectory,
                                                               [apnonceDevice getCpid]]]) {
        apnonceshshPath =
            [NSString stringWithFormat:@"%@/Ramiel/shsh/%@.shsh", documentsDirectory, [apnonceDevice getCpid]];
    } else {
        apnonceshshPath = [NSString stringWithFormat:@"%@/shsh/shsh.shsh", [[NSBundle mainBundle] resourcePath]];
    }

    [RamielView img4toolCMD:[NSString stringWithFormat:@"-c %@/ibss.img4 -p %@/RamielFiles/ibss.%@.patched -s %@",
                                                       [[NSBundle mainBundle] resourcePath],
                                                       [[NSBundle mainBundle] resourcePath], [apnonceDevice getModel],
                                                       apnonceshshPath]];

    [RamielView img4toolCMD:[NSString stringWithFormat:@"-c %@/ibec.img4 -p %@/RamielFiles/ibec.%@.patched -s %@",
                                                       [[NSBundle mainBundle] resourcePath],
                                                       [[NSBundle mainBundle] resourcePath], [apnonceDevice getModel],
                                                       apnonceshshPath]];

    // Boot SSH Ramdisk
    dispatch_async(dispatch_get_main_queue(), ^{
        [self bootDevice];
    });
    return;
}

- (void)bootDevice {

    [self->_prog incrementBy:-100.00];

    dispatch_async(dispatch_get_main_queue(), ^{
        [self->_label setStringValue:@"Booting Device..."];
    });

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        irecv_error_t ret = 0;

        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_prog incrementBy:33.3];
            [self->_label setStringValue:@"Sending iBSS..."];
        });
        NSString *err = @"send iBSS";
        NSString *ibss = [NSString stringWithFormat:@"%@/ibss.img4", [[NSBundle mainBundle] resourcePath]];
        ret = [apnonceDevice sendImage:ibss];
        if (ret == IRECV_E_NO_DEVICE) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [RamielView errorHandler:
                    @"Failed to send iBSS to device":@"Ramiel wasn't able to reconnect to the device after sending iBSS"
                                                    :@"libirecovery returned: IRECV_E_NO_DEVICE"];
            });
            return;
        }
        if ([[apnonceDevice getCpid] containsString:@"8015"] || [[apnonceDevice getCpid] containsString:@"8960"] ||
            [[apnonceDevice getCpid] containsString:@"8965"] ||
            ([[apnonceDevice getCpid] containsString:@"8010"] &&
             ([[apnonceDevice getModel] containsString:@"iPad"] ||
              [[apnonceDevice getModel]
                  containsString:@"9,2"]) /*Seems that only A10 iPads need this to happen, not A10 iPhone/iPods*/)) {
            irecv_reset([apnonceDevice getIRECVClient]);
            [apnonceDevice closeDeviceConnection];
            [apnonceDevice setClient:NULL];
            usleep(1000);
            irecv_client_t temp = NULL;
            irecv_open_with_ecid_and_attempts(&temp, (uint64_t)[apnonceDevice getEcid], 5);
            [apnonceDevice setIRECVClient:temp];

            ret = [apnonceDevice sendImage:ibss];
            if (ret == IRECV_E_NO_DEVICE) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [RamielView errorHandler:
                        @"Failed to send iBSS to device for the second time":
                            @"Ramiel wasn't able to reconnect to the device after sending iBSS for the second time":
                                @"libirecovery returned: IRECV_E_NO_DEVICE"];
                    [self.view.window.contentViewController dismissViewController:self];
                    [apnonceDevice teardown];
                    [apnonceIPSW teardown];
                });
                return;
            }
        }
        if (ret == 1) { // Some tools require a *dummy* file to be sent before we
                        // can boot ibss, this deals with that
            if ([RamielView debugCheck])
                printf("Failed to send iBSS once, reclaiming usb and trying again\n");
            irecv_reset([apnonceDevice getIRECVClient]);
            [apnonceDevice closeDeviceConnection];
            [apnonceDevice setClient:NULL];
            usleep(1000);
            irecv_client_t temp = NULL;
            irecv_open_with_ecid_and_attempts(&temp, (uint64_t)[apnonceDevice getEcid], 5);
            [apnonceDevice setIRECVClient:temp];

            ret = [apnonceDevice sendImage:ibss];
            if (ret == IRECV_E_NO_DEVICE) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [RamielView errorHandler:
                        @"Failed to send iBSS to device for the second time":
                            @"Ramiel wasn't able to reconnect to the device after sending iBSS for the second time":
                                @"libirecovery returned: IRECV_E_NO_DEVICE"];
                    [self.view.window.contentViewController dismissViewController:self];
                    [apnonceDevice teardown];
                    [apnonceIPSW teardown];
                    return;
                });
            }

            if (ret == 1) {

                if ([RamielView debugCheck])
                    printf("Failed to send iBSS twice, sending once more then erroring if it fails again\n");
                ret = [apnonceDevice sendImage:ibss];
                if (ret == IRECV_E_NO_DEVICE) {
                    dispatch_async(dispatch_get_main_queue(), ^{
                        [RamielView errorHandler:
                            @"Failed to send iBSS to device for the third time":
                                @"Ramiel wasn't able to reconnect to the device after sending iBSS for the third time":
                                    @"libirecovery returned: IRECV_E_NO_DEVICE"];
                        return;
                    });
                    return;
                }
            }
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_prog incrementBy:33.3];
            [self->_label setStringValue:@"Sending iBEC..."];
        });
        err = @"send iBEC";
        NSString *ibec = [NSString stringWithFormat:@"%@/ibec.img4", [[NSBundle mainBundle] resourcePath]];
        ret = [apnonceDevice sendImage:ibec];
        sleep(3);
        if ([[apnonceDevice getCpid] containsString:@"8015"]) {
            sleep(2);
        }
        if ([[apnonceDevice getCpid] isEqualToString:@"0x8010"] ||
            [[apnonceDevice getCpid] isEqualToString:@"0x8011"] ||
            [[apnonceDevice getCpid] isEqualToString:@"0x8015"]) {
            ret = [apnonceDevice sendImage:ibec];
            sleep(1);
            err = @"send go command";
            NSString *boot = @"go";
            ret = [apnonceDevice sendCMD:boot];
            sleep(1);
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_prog incrementBy:33.4];
            [self->_label setStringValue:@"Setting generator and rebooting device..."];
        });
        // Send recovery commands now
        ret = [apnonceDevice
            sendCMD:[NSString stringWithFormat:@"setenv com.apple.System.boot-nonce %@", generatorString]];
        ret = [apnonceDevice sendCMD:@"saveenv"];
        ret = [apnonceDevice sendCMD:@"setenv auto-boot false"];
        ret = [apnonceDevice sendCMD:@"saveenv"];
        ret = [apnonceDevice sendCMD:@"reset"];

        if (ret == IRECV_E_SUCCESS) {

            sleep(6);

            dispatch_async(dispatch_get_main_queue(), ^{
                NSAlert *success = [[NSAlert alloc] init];
                [success setInformativeText:@"Ramiel will now exit."];
                [success setMessageText:[NSString
                                            stringWithFormat:@"Successfully set your generator to\n\"%@\"\nYou can now "
                                                             @"futurerestore using your SHSH with that same generator.",
                                                             generatorString]];
                [success addButtonWithTitle:@"Exit"];
                success.window.titlebarAppearsTransparent = true;
                [success runModal];
                exit(0);
            });
        } else {
            dispatch_async(dispatch_get_main_queue(), ^{
                NSAlert *fail = [[NSAlert alloc] init];
                [fail setMessageText:[NSString stringWithFormat:@"Failed to set devices generator. Device returned: %d",
                                                                ret]];
                fail.window.titlebarAppearsTransparent = true;
                [fail runModal];
                [self backButton:nil];
            });
        }
        return;
    });
}

- (int)downloadiBXX {
    [[NSFileManager defaultManager]
              createDirectoryAtPath:[NSString stringWithFormat:@"%@/RamielFiles/", [[NSBundle mainBundle] resourcePath]]
        withIntermediateDirectories:YES
                         attributes:nil
                              error:nil];
    NSURL *IPSWURL = [NSURL
        URLWithString:[NSString stringWithFormat:@"https://api.ipsw.me/v2.1/%@/12.4/url", [apnonceDevice getModel]]];
    NSURLRequest *request = [NSURLRequest requestWithURL:IPSWURL];
    NSData *data = [NSURLConnection sendSynchronousRequest:request returningResponse:nil error:nil];
    NSString *dataString = [[NSString alloc] initWithData:data encoding:NSASCIIStringEncoding];
    NSString *ibssOutPath =
        [NSString stringWithFormat:@"%@/RamielFiles/ibss.im4p", [[NSBundle mainBundle] resourcePath]];
    NSString *ibecOutPath =
        [NSString stringWithFormat:@"%@/RamielFiles/ibec.im4p", [[NSBundle mainBundle] resourcePath]];
    NSString *outPathManifest =
        [NSString stringWithFormat:@"%@/RamielFiles/ApnonceManifest.plist", [[NSBundle mainBundle] resourcePath]];
    [self->_label setStringValue:@"Downloading BuildManifest.plist..."];
    [RamielView downloadFileFromIPSW:dataString:@"BuildManifest.plist":outPathManifest];
    NSDictionary *manifestData = [NSDictionary dictionaryWithContentsOfFile:outPathManifest];
    if (manifestData == NULL) {
        return 1;
    }
    NSString *ibssPath;
    NSString *ibecPath;
    NSArray *buildID = [manifestData objectForKey:@"BuildIdentities"];
    for (int i = 0; i < [buildID count]; i++) {

        if ([buildID[i][@"ApChipID"] isEqual:[apnonceDevice getCpid]]) {

            if ([buildID[i][@"Info"][@"DeviceClass"] isEqual:[apnonceDevice getHardware_model]]) {

                ibssPath = buildID[i][@"Manifest"][@"iBSS"][@"Info"][@"Path"];
                ibecPath = buildID[i][@"Manifest"][@"iBEC"][@"Info"][@"Path"];
            }
        }
    }
    [self->_label setStringValue:@"Downloading iBSS..."];
    [RamielView downloadFileFromIPSW:dataString:ibssPath:ibssOutPath];
    [self->_label setStringValue:@"Downloading iBEC..."];
    [RamielView downloadFileFromIPSW:dataString:ibecPath:ibecOutPath];
    [self->_label setStringValue:@"Downloads complete..."];
    FirmwareKeys *ios12Keys = [[FirmwareKeys alloc] initFirmwareKeysID];
    IPSW *ios12IPSW = [[IPSW alloc] initIPSWID];
    [ios12IPSW setIosVersion:[manifestData objectForKey:@"ProductVersion"]];
    [ios12Keys fetchKeysFromWiki:apnonceDevice:ios12IPSW:manifestData];
    [self->_label setStringValue:@"Decrypting iBSS..."];
    [RamielView img4toolCMD:[NSString stringWithFormat:@"-e -o %@/RamielFiles/ibss.raw --iv %@ "
                                                       @"--key %@ %@/RamielFiles/ibss.im4p",
                                                       [[NSBundle mainBundle] resourcePath], [ios12Keys getIbssIV],
                                                       [ios12Keys getIbssKEY], [[NSBundle mainBundle] resourcePath]]];
    [self->_label setStringValue:@"Decrypting iBEC..."];
    [RamielView img4toolCMD:[NSString stringWithFormat:@"-e -o %@/RamielFiles/ibec.raw --iv %@ "
                                                       @"--key %@ %@/RamielFiles/ibec.im4p",
                                                       [[NSBundle mainBundle] resourcePath], [ios12Keys getIbecIV],
                                                       [ios12Keys getIbecKEY], [[NSBundle mainBundle] resourcePath]]];

    return 0;
}

- (IBAction)backButton:(NSButton *)sender {
    [apnonceDevice teardown];
    [apnonceIPSW teardown];
    [self.view.window.contentViewController dismissViewController:self];
}

@end
