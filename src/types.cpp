// =============================================================================
//  src/types.cpp — Phase 2 stringification
// =============================================================================
#include "tts/types.hpp"

namespace tts {

#define TTS_X(val) case TtfError::val: return "TtfError::" #val;
const char* to_string(TtfError e) noexcept {
    switch (e) {
        TTS_X(Ok)
        TTS_X(InvalidMagicHeader)
        TTS_X(MissingTfdStart)
        TTS_X(MissingTfdEnd)
        TTS_X(MalformedYaml)
        TTS_X(MissingDataStreamMarker)
        TTS_X(MissingEofMarker)
        TTS_X(SectionNotFound)
        TTS_X(OffsetOutOfRange)
        TTS_X(LengthOutOfRange)
        TTS_X(InvalidUtf8Sequence)
        TTS_X(HashMismatch)
        TTS_X(IoError)
        TTS_X(InvalidArgument)
        TTS_X(NotInitialized)
    }
    return "TtfError::<unknown>";
}
#undef TTS_X

#define TTS_X(val) case AttfError::val: return "AttfError::" #val;
const char* to_string(AttfError e) noexcept {
    switch (e) {
        TTS_X(Ok)
        TTS_X(InvalidMagicHeader)
        TTS_X(InvalidVersion)
        TTS_X(MissingDataStream)
        TTS_X(InvalidIndexRow)
        TTS_X(InvalidPageHeader)
        TTS_X(PageNotFound)
        TTS_X(DuplicateAddress)
        TTS_X(AddressParseError)
        TTS_X(InvalidByteSequence)
        TTS_X(IoError)
        TTS_X(InvalidArgument)
        TTS_X(NotInitialized)
    }
    return "AttfError::<unknown>";
}
#undef TTS_X

} // namespace tts
