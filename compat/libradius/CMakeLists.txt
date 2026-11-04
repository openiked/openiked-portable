add_library(radius
	radius.c
	radius_attr.c
	radius_msgauth.c
	radius_mppe.c
	radius_userpass.c
	radius_eapmsk.c)

target_include_directories(radius PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(radius PUBLIC compat)
