idf_component_register(SRCS "time_sync_node.c" "time_sync_root.c" "time_sync_module.c"
                    INCLUDE_DIRS "include"
                    REQUIRES "mac_helper" "mesh_lite" "mesh_lite_comm_module")
