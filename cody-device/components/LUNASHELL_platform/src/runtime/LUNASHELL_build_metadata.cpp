#include "LUNASHELL_build_metadata.hpp"
#include "LUNASHELL_build_metadata_generated.hpp"

// No parameters: returns the original build artifact without reading device time or settings.
const LUNASHELL::FirmwareBuildMetadata &LUNASHELL::firmware_build_metadata()
{
    return build_metadata_detail::kCaptured;
}
