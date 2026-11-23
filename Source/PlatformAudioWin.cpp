// Windows: decode any file Media Foundation understands (AAC, M4A, MP4 / MOV / MKV /
// AVI soundtracks, WMA, ALAC...) to interleaved 32-bit float. No JUCE headers here so
// <windows.h> and the Media Foundation headers stay on their own.
#ifdef _WIN32
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <mfapi.h>
 #include <mfidl.h>
 #include <mfreadwrite.h>
 #include <mferror.h>
 #include <vector>
 #include <string>

namespace
{
template <class T> struct Com
{
    T* p = nullptr;
    Com() = default;
    Com (const Com&) = delete;
    Com& operator= (const Com&) = delete;
    ~Com() { if (p != nullptr) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
};
}

bool crDecodeMediaFoundation (const wchar_t* path, std::vector<float>& out, int& channels, double& sampleRate,
                              double maxSeconds, std::string& error)
{
    const HRESULT co = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
    const bool coInit = SUCCEEDED (co);
    if (FAILED (MFStartup (MF_VERSION, MFSTARTUP_LITE)))
    {
        error = "Media Foundation isn't available on this PC";
        if (coInit) CoUninitialize();
        return false;
    }

    bool ok = false;
    {
        Com<IMFSourceReader> reader;
        if (FAILED (MFCreateSourceReaderFromURL (path, nullptr, &reader)))
            error = "Windows can't open this file type";
        else
        {
            const DWORD audio = (DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM;
            reader->SetStreamSelection ((DWORD) MF_SOURCE_READER_ALL_STREAMS, FALSE);
            reader->SetStreamSelection (audio, TRUE);

            Com<IMFMediaType> wanted;
            MFCreateMediaType (&wanted);
            wanted->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            wanted->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_Float);
            if (FAILED (reader->SetCurrentMediaType (audio, nullptr, wanted.p)))
                error = "There's no audio in this file that Windows can decode";
            else
            {
                Com<IMFMediaType> actual;
                UINT32 ch = 0, rate = 0;
                if (SUCCEEDED (reader->GetCurrentMediaType (audio, &actual)))
                {
                    actual->GetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, &ch);
                    actual->GetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
                }
                channels = (int) ch;
                sampleRate = (double) rate;
                const size_t maxValues = (size_t) (maxSeconds * (double) rate) * (size_t) (ch > 0 ? ch : 1);
                if (ch == 0 || rate == 0) error = "Unknown audio format";
                else
                {
                    for (;;)
                    {
                        DWORD flags = 0;
                        Com<IMFSample> sample;
                        if (FAILED (reader->ReadSample (audio, 0, nullptr, &flags, nullptr, &sample))) break;
                        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) break;
                        if (sample.p == nullptr) continue;
                        Com<IMFMediaBuffer> buffer;
                        if (FAILED (sample->ConvertToContiguousBuffer (&buffer))) break;
                        BYTE* data = nullptr;
                        DWORD length = 0;
                        if (SUCCEEDED (buffer->Lock (&data, nullptr, &length)))
                        {
                            const float* f = reinterpret_cast<const float*> (data);
                            out.insert (out.end(), f, f + length / sizeof (float));
                            buffer->Unlock();
                        }
                        if (out.size() >= maxValues) break;
                    }
                    ok = ! out.empty();
                    if (! ok) error = "The file decoded to silence";
                }
            }
        }
    }
    MFShutdown();
    if (coInit) CoUninitialize();
    return ok;
}
#endif
