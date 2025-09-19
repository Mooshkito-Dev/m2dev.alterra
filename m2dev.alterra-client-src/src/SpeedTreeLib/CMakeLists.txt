file(GLOB_RECURSE FILE_SOURCES "*.h" "*.c" "*.cpp")

add_library(SpeedTreeLib STATIC ${FILE_SOURCES})

target_link_libraries(SpeedTreeLib 
	lzo2
	cryptopp-static
)

GroupSourcesByFolder(SpeedTreeLib)
