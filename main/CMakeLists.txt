# Edit following two lines to set component requirements (see docs)
#set(COMPONENT_REQUIRES "bt")
#set(COMPONENT_REQUIRES )
set(COMPONENT_PRIV_REQUIRES )

set(COMPONENT_SRCS "main.c"
				   "sessionHandler.c"
				   "production_test.c"
				   "storage.c"
				   "connectivity.c"
				   "chargeSession.c"
				   "certificate.c"
				   #"diagnostics_port.c"
				   )

set(COMPONENT_ADD_INCLUDEDIRS "")

register_component()
