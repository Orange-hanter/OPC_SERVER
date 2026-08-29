# OpenSSL 4 made ASN1_STRING opaque. open62541 1.4.11 still reads
# GENERAL_NAME URI fields via ->length / ->data. Accessors exist since
# OpenSSL 1.0.1 and keep Linux (3.x) and Windows (4.x) builds working.
#
# Invoked as FetchContent PATCH_COMMAND with -DFILE=<source>/plugins/crypto/openssl/ua_pki_openssl.c

if(NOT DEFINED FILE OR FILE STREQUAL "")
  message(FATAL_ERROR "open62541 OpenSSL 4 patch: FILE is not set")
endif()
if(NOT EXISTS "${FILE}")
  message(FATAL_ERROR "open62541 OpenSSL 4 patch: missing ${FILE}")
endif()

file(READ "${FILE}" _contents)
set(_original "${_contents}")

string(REPLACE
  "value->d.ia5->length"
  "ASN1_STRING_length(value->d.ia5)"
  _contents "${_contents}")
string(REPLACE
  "value->d.ia5->data"
  "ASN1_STRING_get0_data(value->d.ia5)"
  _contents "${_contents}")

if(_contents STREQUAL _original)
  if(NOT _contents MATCHES "ASN1_STRING_length\\(value->d.ia5\\)")
    message(FATAL_ERROR
      "open62541 OpenSSL 4 patch did not match ${FILE}. "
      "The vendor source no longer contains value->d.ia5->length.")
  endif()
else()
  file(WRITE "${FILE}" "${_contents}")
endif()
