target_sources(${APP_NAME} PRIVATE
	annotations.h annotations.cpp
	colors.h colors.cpp
	components.h components.cpp
	dimred.h dimred.cpp
	distmat.h distmat.cpp
	features.h features.cpp
	hierarchy.h hierarchy.cpp
	)

# mean shift
target_sources(${APP_NAME} PRIVATE
	meanshift/fams.h meanshift/fams.cpp
	meanshift/io.cpp meanshift/mode_pruning.cpp
	)

