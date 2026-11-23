#ifndef WDS_PALETTE_ABI_H
#define WDS_PALETTE_ABI_H

#include <stdint.h>

#define WDS_PALETTE_ABI_VERSION 1u

#if defined(_WIN32) && defined(WDS_BUILD_PALETTE)
#define WDS_PALETTE_EXPORT __declspec(dllexport)
#elif defined(WDS_BUILD_PALETTE)
#define WDS_PALETTE_EXPORT __attribute__((visibility("default")))
#else
#define WDS_PALETTE_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

WDS_PALETTE_EXPORT uint32_t wds_palette_abi_version(void);
WDS_PALETTE_EXPORT uint32_t wds_palette_map_rgb(uint32_t rgb);

#ifdef __cplusplus
}
#endif

#endif
