// macOS: decode the soundtrack of anything AVFoundation can open (MP4, MOV, M4V, M4A,
// AAC, CAF...) to interleaved 32-bit float. No JUCE headers here.
#import <AVFoundation/AVFoundation.h>
#include <vector>
#include <string>

bool crDecodeAVFoundation (const char* utf8Path, std::vector<float>& out, int& channels, double& sampleRate,
                           double maxSeconds, std::string& error)
{
    @autoreleasepool
    {
        NSURL* url = [NSURL fileURLWithPath: [NSString stringWithUTF8String: utf8Path]];
        AVURLAsset* asset = [AVURLAsset URLAssetWithURL: url options: nil];
       #pragma clang diagnostic push
       #pragma clang diagnostic ignored "-Wdeprecated-declarations"
        NSArray<AVAssetTrack*>* tracks = [asset tracksWithMediaType: AVMediaTypeAudio];
       #pragma clang diagnostic pop
        if (tracks == nil || tracks.count == 0) { error = "There's no audio track in this file"; return false; }
        AVAssetTrack* track = tracks[0];

        channels = 2;
        sampleRate = 44100.0;
       #pragma clang diagnostic push
       #pragma clang diagnostic ignored "-Wdeprecated-declarations"
        NSArray* descriptions = track.formatDescriptions;
       #pragma clang diagnostic pop
        if (descriptions.count > 0)
        {
            auto fd = (CMAudioFormatDescriptionRef) (__bridge void*) descriptions[0];
            if (const AudioStreamBasicDescription* asbd = CMAudioFormatDescriptionGetStreamBasicDescription (fd))
            {
                channels = (int) asbd->mChannelsPerFrame;
                if (asbd->mSampleRate > 0) sampleRate = asbd->mSampleRate;
            }
        }
        channels = channels <= 1 ? 1 : 2;   // multichannel soundtracks are folded to stereo

        NSError* err = nil;
        AVAssetReader* reader = [AVAssetReader assetReaderWithAsset: asset error: &err];
        if (reader == nil) { error = "macOS can't read this file"; return false; }
        NSDictionary* settings = @{ AVFormatIDKey: @(kAudioFormatLinearPCM),
                                    AVLinearPCMBitDepthKey: @32,
                                    AVLinearPCMIsFloatKey: @YES,
                                    AVLinearPCMIsBigEndianKey: @NO,
                                    AVLinearPCMIsNonInterleaved: @NO,
                                    AVSampleRateKey: @(sampleRate),
                                    AVNumberOfChannelsKey: @(channels) };
        AVAssetReaderTrackOutput* output = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack: track outputSettings: settings];
        if (! [reader canAddOutput: output]) { error = "macOS can't decode this audio"; return false; }
        [reader addOutput: output];
        if (! [reader startReading]) { error = "macOS couldn't start reading the file"; return false; }

        const size_t maxValues = (size_t) (maxSeconds * sampleRate) * (size_t) channels;
        while (reader.status == AVAssetReaderStatusReading)
        {
            CMSampleBufferRef sb = [output copyNextSampleBuffer];
            if (sb == nullptr) break;
            if (CMBlockBufferRef bb = CMSampleBufferGetDataBuffer (sb))
            {
                const size_t bytes = CMBlockBufferGetDataLength (bb);
                const size_t old = out.size();
                out.resize (old + bytes / sizeof (float));
                CMBlockBufferCopyDataBytes (bb, 0, (bytes / sizeof (float)) * sizeof (float), out.data() + old);
            }
            CFRelease (sb);
            if (out.size() >= maxValues) { [reader cancelReading]; break; }
        }
        if (reader.status == AVAssetReaderStatusFailed) { error = "macOS failed while decoding the file"; return false; }
        if (out.empty()) { error = "The file decoded to silence"; return false; }
        return true;
    }
}
