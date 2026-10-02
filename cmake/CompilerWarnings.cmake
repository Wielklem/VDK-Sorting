function(vsort_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
      -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual
      -Wnull-dereference -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough)
  endif()
endfunction()
