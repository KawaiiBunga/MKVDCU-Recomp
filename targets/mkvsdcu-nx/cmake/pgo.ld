/* Keep the profile pointer table inside the existing writable NRO segment. */
SECTIONS {
  gcov_info : ALIGN(8) {
    __start_gcov_info = .;
    KEEP (*(gcov_info))
    __stop_gcov_info = .;
  } :data
} INSERT AFTER .data;
