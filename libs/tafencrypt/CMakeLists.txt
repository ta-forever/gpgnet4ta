if(NOT DEFINED CRYPTOPP_LIBRARIES)
  if(NOT PKGCONFIG_FOUND)
    find_package(PkgConfig REQUIRED)
  endif()
  pkg_search_module(CRYPTOPP REQUIRED cryptopp)
endif()

function(convert_hex_modulus_to_bytes MODULUS_STRING OUT_VAR)
    string(REPLACE "Modulus=" "" HEX_MODULUS "${MODULUS_STRING}")
    set(DECIMAL_BYTES "")

    # Compute how many bytes (2 hex chars per byte)
    string(LENGTH "${HEX_MODULUS}" HEX_LEN)
    math(EXPR NUM_BYTES "${HEX_LEN} / 2")

    # Iterate over hex string in 2-character chunks
    foreach(I RANGE 0 ${NUM_BYTES})
        math(EXPR POS "2 * ${I}")
        if(POS GREATER_EQUAL ${HEX_LEN})
            break()
        endif()
        string(SUBSTRING "${HEX_MODULUS}" ${POS} 2 BYTE_HEX)
        string(PREPEND BYTE_HEX "0x")
        math(EXPR BYTE_DEC "${BYTE_HEX}")
        list(APPEND DECIMAL_BYTES "${BYTE_DEC}")
    endforeach()

    # Return as comma-separated string
    string(JOIN "," BYTE_STRING ${DECIMAL_BYTES})
    set(${OUT_VAR} "${BYTE_STRING}" PARENT_SCOPE)
endfunction()

if(NOT DEFINED UID_PUBKEY_MODULUS)
  message(WARNING "You need to set UID_PUBKEY_MODULUS to the output of ''openssl rsa -noout -inform PEM -in faf_pub.pem -pubin -modulus''. Using example public key.")
  set(UID_PUBKEY_BYTES
    "200,172,159,159,117,211,197,121,7,80,96,139,82,253,240,67,219,77,244,65,25,119,3,147,242,142,113,95,120,226,30,104,158,211,48,73,96,59,85,198,183,199,146,127,140,87,183,110,75,173,39,218,35,146,181,21,115,29,238,23,204,109,15,252,99,204,103,130,138,78,12,7,65,20,247,29,195,136,20,53,200,221,58,114,11,170,65,151,100,61,139,170,244,158,7,192,99,91,142,217,139,253,106,198,180,112,173,49,106,90,121,163,2,24,206,176,198,187,35,37,111,218,197,202,247,139,30,126,152,38,34,73,25,199,10,194,12,196,144,195,98,48,149,14,219,39,182,154,73,246,96,81,152,95,163,251,8,117,35,226,61,16,164,190,128,239,187,122,78,102,209,233,11,126,80,71,187,78,239,28,48,175,91,51,100,83,165,203,222,119,117,138,82,131,199,90,134,250,62,51,231,180,158,11,109,138,75,37,221,145,184,14,177,203,192,191,48,25,159,137,191,252,5,174,209,207,247,198,32,56,152,65,134,251,180,147,36,250,95,50,253,103,240,3,100,211,86,117,5,63,205,61,176,76,48,209"
  )
else()
  convert_hex_modulus_to_bytes("${UID_PUBKEY_MODULUS}" UID_PUBKEY_BYTES)
endif()

add_definitions(-DUID_PUBKEY_BYTES=${UID_PUBKEY_BYTES})

add_library(tafencrypt STATIC
    ServerEncrypt.h
    ServerEncrypt.cpp
)

target_link_libraries(tafencrypt
    ${CRYPTOPP_LIBRARIES}
)

target_include_directories(tafencrypt
    PUBLIC
    ${CRYPTOPP_INCLUDE_DIRS}
)
