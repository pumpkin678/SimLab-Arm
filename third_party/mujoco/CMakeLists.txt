# Official native SDK only; independent of Python/conda installations.
set(mujoco_version "3.9.0")
set(mujoco_archive "mujoco-${mujoco_version}-windows-x86_64.zip")
set(mujoco_sha256 "544f44a8a7df3e94648a7eaf41500f4456eb59f9f01df3ec2cfb03bdbf5c2bb9")
set(mujoco_sdk_dir "${CMAKE_CURRENT_SOURCE_DIR}/sdk")
set(mujoco_root "${mujoco_sdk_dir}/mujoco-${mujoco_version}")
set(mujoco_receipt "${mujoco_root}/.sdk-sha256")

set(mujoco_ready FALSE)
if(EXISTS "${mujoco_receipt}" AND
   EXISTS "${mujoco_root}/include/mujoco/mujoco.h" AND
   EXISTS "${mujoco_root}/lib/mujoco.lib" AND
   EXISTS "${mujoco_root}/bin/mujoco.dll" AND
   EXISTS "${mujoco_root}/LICENSE" AND
   EXISTS "${mujoco_root}/THIRD_PARTY_NOTICES.txt")
    file(READ "${mujoco_receipt}" mujoco_installed_sha256)
    if(mujoco_installed_sha256 STREQUAL mujoco_sha256)
        set(mujoco_ready TRUE)
    endif()
endif()

if(NOT mujoco_ready)
    if(EXISTS "${mujoco_root}")
        message(FATAL_ERROR
            "Incomplete/unverified SDK at ${mujoco_root}. Move it aside before retrying; it will not be overwritten.")
    endif()
    file(MAKE_DIRECTORY "${mujoco_sdk_dir}")
    set(mujoco_zip "${mujoco_sdk_dir}/${mujoco_archive}")
    set(mujoco_zip_ready FALSE)
    if(EXISTS "${mujoco_zip}")
        file(SHA256 "${mujoco_zip}" mujoco_cached_sha256)
        if(NOT mujoco_cached_sha256 STREQUAL mujoco_sha256)
            message(FATAL_ERROR "MuJoCo archive checksum mismatch: ${mujoco_zip}")
        endif()
        set(mujoco_zip_ready TRUE)
    endif()
    if(NOT mujoco_zip_ready)
        message(STATUS "Downloading official MuJoCo ${mujoco_version} Windows x64 SDK")
        file(DOWNLOAD
            "https://github.com/google-deepmind/mujoco/releases/download/${mujoco_version}/${mujoco_archive}"
            "${mujoco_zip}.part"
            EXPECTED_HASH "SHA256=${mujoco_sha256}"
            TLS_VERIFY ON TIMEOUT 180 INACTIVITY_TIMEOUT 60
            STATUS mujoco_download_status SHOW_PROGRESS)
        list(GET mujoco_download_status 0 mujoco_download_code)
        list(GET mujoco_download_status 1 mujoco_download_message)
        if(NOT mujoco_download_code EQUAL 0)
            message(FATAL_ERROR "MuJoCo download failed: ${mujoco_download_message}")
        endif()
        file(RENAME "${mujoco_zip}.part" "${mujoco_zip}")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${mujoco_zip}" DESTINATION "${mujoco_root}")
    foreach(required IN ITEMS include/mujoco/mujoco.h lib/mujoco.lib bin/mujoco.dll LICENSE THIRD_PARTY_NOTICES.txt)
        if(NOT EXISTS "${mujoco_root}/${required}")
            message(FATAL_ERROR "Official MuJoCo SDK is missing ${required}")
        endif()
    endforeach()
    file(WRITE "${mujoco_receipt}" "${mujoco_sha256}")
else()
    message(STATUS "Reusing official MuJoCo ${mujoco_version} SDK: ${mujoco_root}")
endif()

add_library(mujoco::mujoco SHARED IMPORTED GLOBAL)
set_target_properties(mujoco::mujoco PROPERTIES
    IMPORTED_IMPLIB "${mujoco_root}/lib/mujoco.lib"
    IMPORTED_LOCATION "${mujoco_root}/bin/mujoco.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${mujoco_root}/include")
