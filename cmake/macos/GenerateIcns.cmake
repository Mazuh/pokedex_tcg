# Build the macOS .icns app icon from the single 1024x1024 master PNG.
#
# Run at build time via `cmake -P` (see the if(APPLE) block in CMakeLists.txt), the
# same shape as GenerateVersion.cmake. macOS ships both tools used here — `sips`
# (downscale) and `iconutil` (pack an .iconset folder into an .icns) — so this adds
# no dependency; it is also why the whole thing is macOS-only.
#
# Inputs (passed with -D): SOURCE_ICON, ICONSET_DIR, OUTPUT_FILE.
#
# The .iconset file names are a fixed vocabulary iconutil requires: each logical
# size needs a 1x and a @2x variant, and the @2x of one size is pixel-identical to
# the 1x of the next one up (icon_16x16@2x.png == icon_32x32.png at 32px). So each
# pixel size is rendered ONCE and copied to its second name rather than resampled
# twice — cheaper, and it guarantees the two are actually identical.

if(NOT EXISTS "${SOURCE_ICON}")
    message(FATAL_ERROR "App icon master not found: ${SOURCE_ICON}")
endif()

# Rebuild from scratch: a stale leftover .png in the .iconset would be packed too.
file(REMOVE_RECURSE "${ICONSET_DIR}")
file(MAKE_DIRECTORY "${ICONSET_DIR}")

# size -> the iconset names that want exactly those pixels.
set(sizes 16 32 64 128 256 512 1024)
set(names_16 "icon_16x16.png")
set(names_32 "icon_16x16@2x.png;icon_32x32.png")
set(names_64 "icon_32x32@2x.png")
set(names_128 "icon_128x128.png")
set(names_256 "icon_128x128@2x.png;icon_256x256.png")
set(names_512 "icon_256x256@2x.png;icon_512x512.png")
set(names_1024 "icon_512x512@2x.png")

foreach(size IN LISTS sizes)
    set(first "")
    foreach(name IN LISTS names_${size})
        if(first STREQUAL "")
            set(first "${ICONSET_DIR}/${name}")
            execute_process(
                COMMAND sips -z ${size} ${size} "${SOURCE_ICON}" --out "${first}"
                RESULT_VARIABLE sips_result
                OUTPUT_QUIET
                ERROR_VARIABLE sips_error)
            if(NOT sips_result EQUAL 0)
                # Pass sips' own diagnostic through: without it an unreadable master
                # fails with a message naming neither the file nor the reason.
                message(FATAL_ERROR
                    "sips failed to render the ${size}px app icon from ${SOURCE_ICON}: ${sips_error}")
            endif()
        else()
            # Checked like the tool calls around it: a silently skipped copy would leave
            # the .iconset short one entry, which iconutil packs happily — the build then
            # succeeds and the app just renders a blurry icon at that one size.
            file(COPY_FILE "${first}" "${ICONSET_DIR}/${name}" RESULT copy_result)
            if(NOT copy_result STREQUAL "0")
                message(FATAL_ERROR
                    "could not copy ${first} to ${ICONSET_DIR}/${name}: ${copy_result}")
            endif()
        endif()
    endforeach()
endforeach()

execute_process(
    COMMAND iconutil --convert icns "${ICONSET_DIR}" --output "${OUTPUT_FILE}"
    RESULT_VARIABLE iconutil_result
    OUTPUT_QUIET
    ERROR_VARIABLE iconutil_error)
if(NOT iconutil_result EQUAL 0)
    message(FATAL_ERROR
        "iconutil failed to pack ${ICONSET_DIR} into ${OUTPUT_FILE}: ${iconutil_error}")
endif()
