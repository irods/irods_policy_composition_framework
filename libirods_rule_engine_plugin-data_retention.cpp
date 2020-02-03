
#include "policy_engine.hpp"

#include "rsDataObjTrim.hpp"
#include "physPath.hpp"

namespace pe = irods::policy_engine;

namespace {
    int trim_data_object(
          rsComm_t* _comm
        , const std::string& _object_path
        , const std::string& _source_resource)
    {
        dataObjInp_t obj_inp{};
        rstrcpy(
            obj_inp.objPath,
            _object_path.c_str(),
            sizeof(obj_inp.objPath));
        addKeyVal(
            &obj_inp.condInput,
            RESC_NAME_KW,
            _source_resource.c_str());
        addKeyVal(
            &obj_inp.condInput,
            COPIES_KW,
            "1");
        if(_comm->clientUser.authInfo.authFlag >= LOCAL_PRIV_USER_AUTH) {
            addKeyVal(
                &obj_inp.condInput,
                ADMIN_KW,
                "true" );
        }

        return rsDataObjTrim(_comm, &obj_inp);

    } // trim_data_object

    irods::error data_retention_policy(const pe::context& ctx)
    {
        std::string object_path{}, source_resource{}, destination_resource{};

        // query processor invocation
        if(ctx.parameters.is_array()) {
            using fsp = irods::experimental::filesystem::path;

            std::string tmp_coll_name{}, tmp_data_name{};

            std::tie(tmp_coll_name, tmp_data_name, source_resource) =
                irods::extract_array_parameters<3, std::string>(ctx.parameters);

            object_path = (fsp{tmp_coll_name} / fsp{tmp_data_name}).string();
        }
        else {
            // event handler or direct call invocation
            std::tie(object_path, source_resource, destination_resource) = irods::extract_dataobj_inp_parameters(
                                                                                 ctx.parameters
                                                                               , irods::tag_first_resc);
        }

        auto comm = ctx.rei->rsComm;

        const auto err = trim_data_object(comm, object_path, source_resource);
        if(err < 0) {
            return ERROR(
                       err,
                       boost::format("failed to trim [%s] from [%s]")
                       % object_path
                       % source_resource);
        }

        return SUCCESS();

    } // data_retention_policy

} // namespace

extern "C"
pe::plugin_pointer_type plugin_factory(
      const std::string& _plugin_name
    , const std::string&) {

    return pe::make(_plugin_name, "irods_policy_data_retention", data_retention_policy);

} // plugin_factory
