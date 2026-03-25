#include "rlp/common.hpp"
#include <sstream>
#include <iomanip>

namespace rlp {

    const char* encoding_error_to_string(EncodingError err) noexcept {
        switch (err) {
            case EncodingError::kPayloadTooLarge: return "RLP Encoding: Payload exceeds 64-bit limit";
            case EncodingError::kEmptyInput: return "RLP Encoding: Empty input not allowed";
            case EncodingError::kUnclosedList: return "RLP Encoding: Unclosed list";
            case EncodingError::kUnmatchedEndList: return "RLP Encoding: EndList without matching BeginList";
            default: return "RLP Encoding: Unknown Error";
        }
    }

    const char* decoding_error_to_string(DecodingError err) noexcept {
        switch (err) {
            case DecodingError::kOverflow: return "RLP Overflow";
            case DecodingError::kLeadingZero: return "RLP Leading Zero";
            case DecodingError::kInputTooShort: return "RLP Input Too Short";
            case DecodingError::kInputTooLong: return "RLP Input Too Long";
            case DecodingError::kNonCanonicalSize: return "RLP Non-Canonical Size";
            case DecodingError::kUnexpectedList: return "RLP Unexpected List";
            case DecodingError::kUnexpectedString: return "RLP Unexpected String";
            case DecodingError::kUnexpectedListElements: return "RLP Unexpected List Elements";
            case DecodingError::kInvalidVrsValue: return "RLP Invalid VRS Value";
            case DecodingError::kListLengthMismatch: return "RLP List Length Mismatch";
            case DecodingError::kNotInList: return "RLP Operation requires being in a list context";
            case DecodingError::kMalformedHeader: return "RLP Malformed Header";
            case DecodingError::kAlreadyFinalized: return "RLP Already Finalized";
            case DecodingError::kNotFinalized: return "RLP Not Finalized";
            case DecodingError::kInvalidChunkSize: return "RLP Invalid Chunk Size";
            case DecodingError::kHeaderSizeExceeded: return "RLP Header Size Exceeded";
            default: return "RLP Unknown Error";
        }
    }

    const char* streaming_error_to_string(StreamingError err) noexcept {
        switch (err) {
            case StreamingError::kAlreadyFinalized: return "RLP Streaming: Already finalized";
            case StreamingError::kNotFinalized: return "RLP Streaming: Not finalized";
            case StreamingError::kInvalidChunkSize: return "RLP Streaming: Invalid chunk size";
            case StreamingError::kHeaderSizeExceeded: return "RLP Streaming: Header size exceeded";
            default: return "RLP Streaming: Unknown Error";
        }
    }

    std::string hexToString(ByteView bv) noexcept {
        std::stringstream ss;
        ss << std::setfill('0') << std::hex;
        for (auto &byte : bv) {
            ss << std::setw(2) << (int)byte;
        }
        return ss.str();
    }

} // namespace rlp
