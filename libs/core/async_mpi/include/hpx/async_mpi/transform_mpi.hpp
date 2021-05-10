//  Copyright (c) 2007-2021 Hartmut Kaiser
//  Copyright (c) 2021 Giannis Gonidelis
//
//  SPDX-License-Identifier: BSL-1.0
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

/// \file parallel/algorithms/transform_xxx.hpp

#pragma once

#include <hpx/config.hpp>
#include <hpx/async_mpi/mpi_future.hpp>
#include <hpx/concepts/concepts.hpp>
#include <hpx/datastructures/tuple.hpp>
#include <hpx/execution/algorithms/detail/partial_algorithm.hpp>
#include <hpx/execution_base/receiver.hpp>
#include <hpx/execution_base/sender.hpp>
#include <hpx/functional/invoke.hpp>
#include <hpx/functional/tag_fallback_dispatch.hpp>
#include <hpx/functional/traits/is_invocable.hpp>
#include <hpx/mpi_base/mpi.hpp>

#include <exception>
#include <type_traits>
#include <utility>

namespace hpx { namespace mpi { namespace experimental {
    namespace detail {

        template <typename R, typename... Ts>
        void set_value_request_callback_helper(
            int mpi_status, R&& r, Ts&&... ts)
        {
            static_assert(sizeof...(Ts) <= 1, "Expecting at most one value");
            if (mpi_status == MPI_SUCCESS)
            {
                hpx::execution::experimental::set_value(
                    std::forward<R>(r), std::forward<Ts>(ts)...);
            }
            else
            {
                hpx::execution::experimental::set_error(std::forward<R>(r),
                    std::make_exception_ptr(mpi_exception(mpi_status)));
            }
        }

        template <typename R, typename... Ts>
        void set_value_request_callback_void(
            MPI_Request request, R&& r, Ts&&... ts)
        {
            detail::add_request_callback(
                [r = std::forward<R>(r),
                    keep_alive = hpx::make_tuple(std::forward<Ts>(ts)...)](
                    int status) mutable {
                    set_value_request_callback_helper(status, std::move(r));
                },
                request);
        }

        template <typename R, typename InvokeResult, typename... Ts>
        void set_value_request_callback_non_void(
            MPI_Request request, R&& r, InvokeResult&& res, Ts&&... ts)
        {
            detail::add_request_callback(
                [r = std::forward<R>(r), res = std::forward<InvokeResult>(res),
                    keep_alive = hpx::make_tuple(std::forward<Ts>(ts)...)](
                    int status) mutable {
                    set_value_request_callback_helper(
                        status, std::move(r), std::move(res));
                },
                request);
        }

        template <typename R, typename F>
        struct transform_mpi_receiver
        {
            std::decay_t<R> r;
            std::decay_t<F> f;

            template <typename R_, typename F_>
            transform_mpi_receiver(R_&& r, F_&& f)
              : r(std::forward<R_>(r))
              , f(std::forward<F_>(f))
            {
            }

            template <typename E>
            friend constexpr void tag_dispatch(
                hpx::execution::experimental::set_error_t,
                transform_mpi_receiver&& r, E&& e) noexcept
            {
                hpx::execution::experimental::set_error(
                    std::move(r.r), std::forward<E>(e));
            }

            friend constexpr void tag_dispatch(
                hpx::execution::experimental::set_done_t,
                transform_mpi_receiver&& r) noexcept
            {
                hpx::execution::experimental::set_done(std::move(r.r));
            };

            template <typename... Ts,
                typename = std::enable_if_t<
                    hpx::is_invocable_v<F, Ts..., MPI_Request*>>>
            friend constexpr void tag_dispatch(
                hpx::execution::experimental::set_value_t,
                transform_mpi_receiver&& r, Ts&&... ts) noexcept
            {
                hpx::detail::try_catch_exception_ptr(
                    [&]() {
                        if constexpr (std::is_void_v<util::invoke_result_t<F,
                                          Ts..., MPI_Request*>>)
                        {
                            MPI_Request request;
                            HPX_INVOKE(r.f, ts..., &request);
                            // When the return type is void, there is no value
                            // to forward to the receiver
                            set_value_request_callback_void(request,
                                std::move(r.r), std::forward<Ts>(ts)...);
                        }
                        else
                        {
                            MPI_Request request;
                            // When the return type is non-void, we have to
                            // forward the value to the receiver
                            auto&& result = HPX_INVOKE(
                                r.f, std::forward<Ts>(ts)..., &request);
                            set_value_request_callback_non_void(request,
                                std::move(r.r), std::move(result),
                                std::forward<Ts>(ts)...);
                        }
                    },
                    [&](std::exception_ptr ep) {
                        hpx::execution::experimental::set_error(
                            std::move(r.r), std::move(ep));
                    });
            }
        };

        template <typename Sender, typename F>
        struct transform_mpi_sender
        {
            std::decay_t<Sender> s;
            std::decay_t<F> f;

            template <typename Tuple>
            struct invoke_result_helper;

            template <template <typename...> class Tuple, typename... Ts>
            struct invoke_result_helper<Tuple<Ts...>>
            {
                static_assert(hpx::is_invocable_v<F, Ts..., MPI_Request*>,
                    "F not invocable with the value_types specified.");
                using result_type =
                    hpx::util::invoke_result_t<F, Ts..., MPI_Request*>;
                using type =
                    std::conditional_t<std::is_void<result_type>::value,
                        Tuple<>, Tuple<result_type>>;
            };

            template <template <typename...> class Tuple,
                template <typename...> class Variant>
            using value_types =
                hpx::util::detail::unique_t<hpx::util::detail::transform_t<
                    typename hpx::execution::experimental::sender_traits<
                        Sender>::template value_types<Tuple, Variant>,
                    invoke_result_helper>>;

            template <template <typename...> class Variant>
            using error_types =
                hpx::util::detail::unique_t<hpx::util::detail::prepend_t<
                    typename hpx::execution::experimental::sender_traits<
                        Sender>::template error_types<Variant>,
                    std::exception_ptr>>;

            static constexpr bool sends_done = false;

            template <typename R>
            friend constexpr auto tag_dispatch(
                hpx::execution::experimental::connect_t,
                transform_mpi_sender& s, R&& r)
            {
                return hpx::execution::experimental::connect(
                    s.s, transform_mpi_receiver<R, F>(std::forward<R>(r), s.f));
            }

            template <typename R>
            friend constexpr auto tag_dispatch(
                hpx::execution::experimental::connect_t,
                transform_mpi_sender&& s, R&& r)
            {
                return hpx::execution::experimental::connect(std::move(s.s),
                    transform_mpi_receiver<R, F>(
                        std::forward<R>(r), std::move(s.f)));
            }
        };
    }    // namespace detail

    HPX_INLINE_CONSTEXPR_VARIABLE struct transform_mpi_t final
      : hpx::functional::tag_fallback<transform_mpi_t>
    {
    private:
        template <typename Sender, typename F,
            HPX_CONCEPT_REQUIRES_(
                hpx::execution::experimental::is_sender_v<Sender>)>
        friend constexpr HPX_FORCEINLINE auto tag_fallback_dispatch(
            transform_mpi_t, Sender&& s, F&& f)
        {
            return detail::transform_mpi_sender<Sender, F>{
                std::forward<Sender>(s), std::forward<F>(f)};
        }

        template <typename F>
        friend constexpr HPX_FORCEINLINE auto tag_fallback_dispatch(
            transform_mpi_t, F&& f)
        {
            return ::hpx::execution::experimental::detail::partial_algorithm<
                transform_mpi_t, F>{std::forward<F>(f)};
        }
    } transform_mpi{};
}}}    // namespace hpx::mpi::experimental
