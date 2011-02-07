//  Copyright (c) 2007-2011 Hartmut Kaiser
// 
//  Distributed under the Boost Software License, Version 1.0. (See accompanying 
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#if !defined(HPX_RUNTIME_SUPPORT_JUN_02_2008_1145AM)
#define HPX_RUNTIME_SUPPORT_JUN_02_2008_1145AM

#include <map>
#include <list>

#include <boost/thread.hpp>
#include <boost/thread/condition.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/plugin.hpp>

#include <hpx/hpx_fwd.hpp>
#include <hpx/runtime/components/component_type.hpp>
#include <hpx/runtime/components/component_factory_base.hpp>
#include <hpx/runtime/components/constructor_argument.hpp>
#include <hpx/runtime/actions/component_action.hpp>
#include <hpx/runtime/actions/manage_object_action.hpp>

#include <hpx/config/warnings_prefix.hpp>

namespace hpx { namespace components { namespace server
{
    ///////////////////////////////////////////////////////////////////////////
    class runtime_support
    {
    private:
        typedef boost::mutex mutex_type;
        typedef std::pair<
            boost::shared_ptr<component_factory_base>, boost::plugin::dll
        > component_factory_type;
        typedef std::map<component_type, component_factory_type> component_map_type;

    public:
        typedef runtime_support type_holder;

        // parcel action code: the action to be performed on the destination 
        // object 
        enum actions
        {
            runtime_support_factory_properties = 0, ///< return whether more than 
                                                    ///< one instance of a component 
                                                    ///< can be created at once
            runtime_support_create_component = 1,   ///< create new components
            runtime_support_create_one_component = 2,   ///< create new component with one constructor argument
            runtime_support_free_component = 3,     ///< delete existing components
            runtime_support_shutdown = 4,           ///< shut down this runtime instance
            runtime_support_shutdown_all = 5,       ///< shut down the runtime instances of all localities

            runtime_support_get_config = 6,         ///< get configuration information 
            runtime_support_create_memory_block = 7,   ///< create new memory block
        };

        static component_type get_component_type() 
        { 
            return components::get_component_type<runtime_support>(); 
        }
        static void set_component_type(component_type t) 
        { 
            components::set_component_type<runtime_support>(t); 
        }

        // constructor
        runtime_support(util::section& ini, naming::gid_type const& prefix, 
                naming::resolver_client& agas_client, applier::applier& applier);

        ~runtime_support()
        {
            tidy();
        }

        /// \brief finalize() will be called just before the instance gets 
        ///        destructed
        ///
        /// \param self [in] The PX \a thread used to execute this function.
        /// \param appl [in] The applier to be used for finalization of the 
        ///             component instance. 
        void finalize() {}

        void tidy();

        ///////////////////////////////////////////////////////////////////////
        // exposed functionality of this component

        /// \brief Action to figure out, whether we can create more than one 
        ///        instance at once
        int factory_properties(components::component_type type); 

        /// \brief Action to create new components
        naming::gid_type create_component(components::component_type type, 
            std::size_t count); 

        /// \brief Action to create new component while passing one constructor 
        ///        parameter
        naming::gid_type create_one_component(components::component_type type, 
            constructor_argument const& arg0); 

        /// \brief Action to create new memory block
        naming::gid_type create_memory_block(std::size_t count, 
            hpx::actions::manage_object_action_base const& act); 

        /// \brief Action to delete existing components
        void free_component(components::component_type type, 
            naming::gid_type const& gid); 

        /// \brief Action shut down this runtime system instance
        int shutdown();

        /// \brief Action shut down runtime system instances on all localities
        void shutdown_all();

        /// \brief Retrieve configuration information
        util::section get_config();

        ///////////////////////////////////////////////////////////////////////
        // Each of the exposed functions needs to be encapsulated into a action
        // type, allowing to generate all require boilerplate code for threads,
        // serialization, etc.
#ifdef STACKLESS_COROUTINE
        typedef hpx::actions::direct_result_action1<
            runtime_support, int,
            runtime_support_factory_properties, components::component_type,
            &runtime_support::factory_properties
        > factory_properties_action;

        typedef hpx::actions::direct_result_action2<
            runtime_support, naming::gid_type, runtime_support_create_component,
            components::component_type, std::size_t,
            &runtime_support::create_component
        > create_component_action;

        typedef hpx::actions::direct_result_action2<
            runtime_support, naming::gid_type,
            runtime_support_create_one_component,
            components::component_type, constructor_argument const&,
            &runtime_support::create_one_component
        > create_one_component_action;

        typedef hpx::actions::direct_result_action2<
            runtime_support, naming::gid_type, runtime_support_create_memory_block,
            std::size_t, hpx::actions::manage_object_action_base const&,
            &runtime_support::create_memory_block
        > create_memory_block_action;
#else
        typedef hpx::actions::result_action1<
            runtime_support, int, 
            runtime_support_factory_properties, components::component_type, 
            &runtime_support::factory_properties
        > factory_properties_action;

        typedef hpx::actions::result_action2<
            runtime_support, naming::gid_type, runtime_support_create_component, 
            components::component_type, std::size_t, 
            &runtime_support::create_component
        > create_component_action;

        typedef hpx::actions::result_action2<
            runtime_support, naming::gid_type, 
            runtime_support_create_one_component, 
            components::component_type, constructor_argument const&, 
            &runtime_support::create_one_component
        > create_one_component_action;

        typedef hpx::actions::result_action2<
            runtime_support, naming::gid_type, runtime_support_create_memory_block, 
            std::size_t, hpx::actions::manage_object_action_base const&,
            &runtime_support::create_memory_block
        > create_memory_block_action;
#endif
        typedef hpx::actions::direct_action2<
            runtime_support, runtime_support_free_component, 
            components::component_type, naming::gid_type const&, 
            &runtime_support::free_component
        > free_component_action;

        typedef hpx::actions::result_action0<
            runtime_support, int, runtime_support_shutdown, 
            &runtime_support::shutdown
        > shutdown_action;

        typedef hpx::actions::action0<
            runtime_support, runtime_support_shutdown_all, 
            &runtime_support::shutdown_all
        > shutdown_all_action;

        // even if this is not a short/minimal action, we still execute it 
        // directly to avoid a deadlock condition inside the thread manager
        // waiting for this thread to finish, which waits for the thread 
        // manager to exit
        typedef hpx::actions::direct_result_action0<
            runtime_support, util::section, runtime_support_get_config, 
            &runtime_support::get_config
        > get_config_action;

        /// \brief Start the runtime_support component
        void run();

        /// \brief Wait for the runtime_support component to notify the calling
        ///        thread.
        ///
        /// This function will be called from the main thread, causing it to
        /// block while the HPX functionality is executed. The main thread will
        /// block until the shutdown_action is executed, which in turn notifies
        /// all waiting threads.
        void wait();

        /// \brief Notify all waiting (blocking) threads allowing the system to 
        ///        be properly stopped.
        ///
        /// \note      This function can be called from any thread.
        void stop();

        /// called locally only
        void stopped();

        bool was_stopped() const { return stopped_; }

    protected:
        // Load all components from the ini files found in the configuration
        void load_components(util::section& ini, naming::gid_type const& prefix, 
            naming::resolver_client& agas_client);
        bool load_component(util::section& ini, std::string const& instance, 
            std::string const& component, boost::filesystem::path lib,
            naming::gid_type const& prefix, naming::resolver_client& agas_client, 
            bool isdefault);

    private:
        mutex_type mtx_;
        boost::condition wait_condition_;
        boost::condition stop_condition_;
        bool stopped_;
        bool terminated_;

        component_map_type components_;
        util::section& ini_;
    };

}
    template <>
    struct HPX_ALWAYS_EXPORT component_type_database<runtime_support>
    {
        static component_type get()
        {
            return components::component_runtime_support; 
        }

        static void set(component_type type)
        {
            BOOST_ASSERT(false);
        }
    }; 
}}

#include <hpx/config/warnings_suffix.hpp>

#endif
