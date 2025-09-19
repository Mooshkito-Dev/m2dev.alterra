add_subdirectory(cryptopp)
add_subdirectory(lzo-2.10)

if (WIN32)
	set_target_properties(cryptopp-static PROPERTIES FOLDER vendor)
	set_target_properties(lzo2 PROPERTIES FOLDER vendor)
endif()