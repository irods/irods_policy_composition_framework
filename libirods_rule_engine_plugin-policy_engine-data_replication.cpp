
#include "policy_engine.hpp"
#include "filesystem.hpp"

#include "irods_hierarchy_parser.hpp"
#include "irods_server_api_call.hpp"
#include "exec_as_user.hpp"

#include "physPath.hpp"
#include "apiNumber.h"

namespace {
    int replicate_object_to_resource(
          rsComm_t*          _comm
        , const std::string& _user_name
        , const std::string& _object_path
        , const std::string& _source_resource
        , const std::string& _destination_resource)
    {
        dataObjInp_t data_obj_inp{};
        rstrcpy(data_obj_inp.objPath, _object_path.c_str(), MAX_NAME_LEN);
        data_obj_inp.createMode = getDefFileMode();
        addKeyVal(&data_obj_inp.condInput, RESC_NAME_KW,      _source_resource.c_str());
        addKeyVal(&data_obj_inp.condInput, DEST_RESC_NAME_KW, _destination_resource.c_str());

        if(_comm->clientUser.authInfo.authFlag >= LOCAL_PRIV_USER_AUTH) {
            addKeyVal(&data_obj_inp.condInput, ADMIN_KW, "true" );
        }

        transferStat_t* trans_stat{};

        //const auto repl_err = irods::server_api_call(DATA_OBJ_REPL_AN, _comm, &data_obj_inp, &trans_stat);
        auto repl_fcn = [&](auto& comm){
            auto ret = irods::server_api_call(DATA_OBJ_REPL_AN, _comm, &data_obj_inp, &trans_stat);
            free(trans_stat);
            return ret;};

        return irods::exec_as_user(*_comm, _user_name, repl_fcn);

    } // replicate_object_to_resource

    namespace pe = irods::policy_engine;

    irods::error replication_policy(const pe::context ctx)
    {
        std::string user_name{}, object_path{}, source_resource{}, destination_resource{};

        // query processor invocation
        if(ctx.parameters.is_array()) {
            using fsp = irods::experimental::filesystem::path;

            std::string tmp_coll_name{}, tmp_data_name{};

            std::tie(tmp_coll_name, tmp_data_name, source_resource) = irods::extract_array_parameters<3, std::string>(ctx.parameters);

            object_path = (fsp{tmp_coll_name} / fsp{tmp_data_name}).string();
        }
        else {
            // event handler or direct call invocation
            std::tie(user_name, object_path, source_resource, destination_resource) =
                irods::extract_dataobj_inp_parameters(
                      ctx.parameters
                    , irods::tag_first_resc);
        }

        auto comm = ctx.rei->rsComm;

        if(!destination_resource.empty()) {

            // direct call invocation
            int err = replicate_object_to_resource(
                            comm
                          , user_name
                          , object_path
                          , source_resource
                          , destination_resource);
            if(err < 0) {
                return ERROR(
                          err,
                          boost::format("failed to replicate [%s] from [%s] to [%s]")
                          % object_path
                          % source_resource
                          % destination_resource);
            }
        }
        else {
            // event handler invocation
            if(ctx.configuration.empty()) {
                THROW(
                    SYS_INVALID_INPUT_PARAM,
                    boost::format("%s - destination_resource is empty and configuration is not provided")
                    % ctx.policy_name);
            }

            destination_resource = irods::extract_object_parameter<std::string>("destination_resource", ctx.configuration);
            if(!destination_resource.empty()) {
                int err = replicate_object_to_resource(
                                comm
                              , user_name
                              , object_path
                              , source_resource
                              , destination_resource);
                if(err < 0) {
                    return ERROR(
                              err,
                              boost::format("failed to replicate [%s] from [%s] to [%s]")
                              % object_path
                              % source_resource
                              % destination_resource);
                }
            }
            else {
                auto src_dst_map{ctx.configuration.at("source_to_destination_map")};
                if(src_dst_map.empty()) {
                    THROW(
                        SYS_INVALID_INPUT_PARAM,
                        boost::format("%s - destination_resource or source_to_destination_map not provided")
                        % ctx.policy_name);
                }

                auto dst_resc_arr{src_dst_map.at(source_resource)};
                auto destination_resources = dst_resc_arr.get<std::vector<std::string>>();
                irods::error ret{SUCCESS()};
                for( auto& dest : destination_resources) {
                    int err = replicate_object_to_resource(
                                    comm
                                  , user_name
                                  , object_path
                                  , source_resource
                                  , dest);
                    if(err < 0) {
                        ret = PASSMSG(
                                  boost::str(boost::format("failed to replicate [%s] from [%s] to [%s]")
                                  % object_path
                                  % source_resource),
                                  ret);
                    }
                }

                if(!ret.ok()) {
                    return ret;
                }
            }
        }

        return SUCCESS();

    } // replication_policy

} // namespace

extern "C"
pe::plugin_pointer_type plugin_factory(
      const std::string& _plugin_name
    , const std::string&)
{
    return pe::make(
             _plugin_name
            , "irods_policy_data_replication"
            , replication_policy);
} // plugin_factory
