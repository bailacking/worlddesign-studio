#include "palette_abi.h"

uint32_t wds_palette_abi_version() { return WDS_SAMPLE_ABI; }

uint32_t wds_palette_map_rgb(uint32_t rgb) { return (rgb & 0xffff00u) | WDS_SAMPLE_BLUE; }
