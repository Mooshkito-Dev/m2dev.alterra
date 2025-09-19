file(GLOB_RECURSE FILE_SOURCES "*.h" "*.c" "*.cpp")

add_library(EterPack STATIC ${FILE_SOURCES})

target_link_libraries(EterPack 
	lzo2
	cryptopp-static
)

GroupSourcesByFolder(EterPack)
