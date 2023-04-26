if(USE_TutorialC)
  file(GLOB CSOURCE_RELAY_CONTRIB_SRC src/relay/backend/contrib/codegen_c/codegen.cc)
  list(APPEND COMPILER_SRCS ${CSOURCE_RELAY_CONTRIB_SRC})
endif(USE_TutorialC)