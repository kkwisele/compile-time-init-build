#pragma once

#include <msg/message.hpp>
#include <type_traits>
#include <tuple>

#include <container/Vector.hpp>
#include <container/Array.hpp>
#include <cib/tuple.hpp>

#include <log/log.hpp>

namespace msg {
    template<
        typename BaseMsgT,
        typename... ExtraCallbackArgsT>
    struct Callback {
        [[nodiscard]] virtual bool is_match(BaseMsgT const & msg) const = 0;
        [[nodiscard]] virtual bool handle(BaseMsgT const & msg, ExtraCallbackArgsT const & ... args) const = 0;
        virtual void log_mismatch(BaseMsgT const & msg) const = 0;
    };

    template<typename T>
    using remove_cvref_t = typename std::remove_cv< typename std::remove_reference<T>::type >::type;



    template<typename...>
    struct callback_types {};

    template<typename...>
    struct extra_callback_args {};

    template<
        typename BaseMsgT,
        typename ExtraCallbackArgsListT,
        typename NameTypeT,
        typename MatchMsgTypeT,
        typename CallbackTypeListT>
    struct callback_impl;

    template<typename CallableT>
    struct func_args {
         using msg_type = typename func_args<decltype(&remove_cvref_t<CallableT>::operator())>::msg_type;
         using type = typename func_args<decltype(&remove_cvref_t<CallableT>::operator())>::type;
    };

    template<typename DataIterableT, typename... ArgTs>
    struct func_args<void(DataIterableT, ArgTs...)> {
        using msg_type = remove_cvref_t<DataIterableT>;
        using type = cib::tuple<ArgTs...>;
    };

    template<typename DataIterableT, typename... ArgTs>
    struct func_args<void(DataIterableT, ArgTs...) const> {
        using msg_type = remove_cvref_t<DataIterableT>;
        using type = cib::tuple<ArgTs...>;
    };

    template<typename DataIterableT, typename T, typename... ArgTs>
    struct func_args<void(T::*)(DataIterableT, ArgTs...)> {
        using msg_type = remove_cvref_t<DataIterableT>;
        using type = cib::tuple<ArgTs...>;
    };

    template<typename DataIterableT, typename T, typename... ArgTs>
    struct func_args<void(T::*)(DataIterableT, ArgTs...) const> {
        using msg_type = remove_cvref_t<DataIterableT>;
        using type = cib::tuple<ArgTs...>;
    };

    template<typename CallableT>
    using func_args_t = typename func_args<CallableT>::type;

    template<typename CallableT>
    constexpr func_args_t<CallableT> func_args_v{};


    template<typename CallbackType>
    using msg_type_t = typename func_args<CallbackType>::msg_type;


    template <
        typename CallableT,
        typename DataIterableT,
        typename... ExtraCallbackArgsT>
    void dispatch_single_callable(
        CallableT const & callable,
        DataIterableT const & data,
        ExtraCallbackArgsT const & ... args
    ) {
        auto const provided_args_tuple = std::tuple(args...);
        auto const required_args_tuple =
            cib::transform(
                func_args_v<CallableT>,
                [&](auto requiredArg){
                    using RequiredArgType = decltype(requiredArg);
                    return std::get<RequiredArgType>(provided_args_tuple);
                }
            );

        required_args_tuple.apply([&](auto const & ... requiredArgs){
            using MsgType = msg_type_t<decltype(callable)>;
            MsgType const msg{data};

            if (msg.isValid()) {
                callable(msg, requiredArgs...);
            }
        });
    }

    /**
     * A Class that defines a message callback and provides methods for validating
     * and handling incoming messages.
     */
    template<
        typename BaseMsgT,
        typename... ExtraCallbackArgsT,
        typename NameTypeT,
        typename MatchMsgTypeT,
        typename... CallableTypesT>
    class callback_impl<
        BaseMsgT,
        extra_callback_args<ExtraCallbackArgsT...>,
        NameTypeT,
        MatchMsgTypeT,
        callback_types<CallableTypesT...>
    >
        : public Callback<BaseMsgT, ExtraCallbackArgsT...>
    {
    private:
        constexpr static NameTypeT name{};

        MatchMsgTypeT match_msg;
        cib::tuple<CallableTypesT...> callbacks;

        template<typename DataIterableType>
        void dispatch(DataIterableType const & data, ExtraCallbackArgsT const & ... args) const {
            callbacks.for_each([&](auto callback){
                dispatch_single_callable(callback, data, args...);
            });
        }

        [[nodiscard]] constexpr auto match_any_callback() const {
            auto const matchers = cib::transform(callbacks, [&](auto callback){
                using MsgType = msg_type_t<decltype(callback)>;
                return is_valid_msg<MsgType>(match::always<true>);
            });

            return matchers.apply([](auto... matchersPack){
                return match::any(matchersPack...);
            });
        }

    public:
        constexpr callback_impl(
            MatchMsgTypeT const & match_msg,
            CallableTypesT const & ... callback_args
        )
            : match_msg(match_msg)
            , callbacks(cib::make_tuple(callback_args...))
        {
            callbacks.for_each([](auto callback) {
                static_assert(
                    !std::is_same<decltype(callback), std::nullptr_t>::value,
                    "Function pointer specified for callback can't be null");
            });
        }

        [[nodiscard]] bool is_match(BaseMsgT const & msg) const final override {
            return match::all(match_msg, match_any_callback())(msg);
        }

        [[nodiscard]] bool handle(BaseMsgT const & msg, ExtraCallbackArgsT const & ... args) const final override {
            auto match_handler =
                match::all(match_msg, match_any_callback());

            if (match_handler(msg)) {
                INFO(
                    "Incoming message matched [{}], because [{}], executing callback",
                    name, match_handler.describe());

                dispatch(msg.data, args...);

                return true;
            }

            return false;
        }

        void log_mismatch(BaseMsgT const & msg) const final override {
            INFO("    {} - F:({})", name,
                 match::all(match_msg, match_any_callback()).describe_match(msg));
        }
    };




    template <
        typename BaseMsgT,
        size_t NumMsgCallbacksT,
        typename... ExtraCallbackArgsT>
    class handler {
    private:
        using CallbackType = Callback<BaseMsgT, ExtraCallbackArgsT...>;
        using CallbacksType = Array<CallbackType const *, NumMsgCallbacksT>;
        CallbacksType callbacks{};

    public:
        constexpr handler(
            CallbacksType callbacks_arg
        )
            : callbacks{callbacks_arg}
        {
            // pass
        }

        constexpr handler(handler const & rhs) = default;
        constexpr handler &operator=(handler const & rhs) = default;

        bool is_match(BaseMsgT const & msg) const {
            for (auto callback : callbacks) {
                if (
                    callback->is_match(msg)
                ) {
                    return true;
                }
            }

            return false;
        }

        void handle(BaseMsgT const & msg, ExtraCallbackArgsT const & ... args) const {
            bool found_valid_callback = false;
            for (auto callback : callbacks) {
                if (callback->handle(msg, args...)) {
                    found_valid_callback = true;
                }
            }

            if (!found_valid_callback) {
                ERROR("None of the registered callbacks claimed this message:");

                for (auto callback : callbacks) {
                    callback->log_mismatch(msg);
                }
            }
        }
    };


    template<
        typename AbstractInterface,
        typename Derived,
        typename BaseMsgT,
        typename... ExtraCallbackArgsT>
    class handler_builder {
    public:
        static constexpr auto MAX_SIZE = 256;
        using CallbacksType = Vector<Callback<BaseMsgT, ExtraCallbackArgsT...> const *, MAX_SIZE>;

    private:
        CallbacksType callbacks;

        template<size_t NumMsgCallbacksT>
        [[nodiscard]] constexpr handler<BaseMsgT, NumMsgCallbacksT, ExtraCallbackArgsT...> build_backend() const {
            Array<Callback<BaseMsgT, ExtraCallbackArgsT...> const *, NumMsgCallbacksT> new_msg_callbacks;

            for (std::size_t i = 0; i < callbacks.size(); i++) {
                new_msg_callbacks[i] = callbacks[i];
            }

            return {new_msg_callbacks};
        }

    public:
        constexpr handler_builder()
            : callbacks{}
        {
            // pass
        }

        constexpr void add(Callback<BaseMsgT, ExtraCallbackArgsT...> const & callback) {
            callbacks.push(&callback);
        }

        [[nodiscard]] constexpr size_t get_num_callbacks() const {
            return callbacks.size();
        }

        template<size_t NumMsgCallbacksT>
        [[nodiscard]] constexpr auto internal_build() const {
            auto const backend = build_backend<NumMsgCallbacksT>();
            auto const frontend = Derived::build_frontend(backend);

            return frontend;
        }


        ///////////////////////////////////////////////////////////////////////////
        ///
        /// Everything below is for the cib extension interface. It lets cib know
        /// this builder supports the cib pattern and how to build it.
        ///
        ///////////////////////////////////////////////////////////////////////////
        /**
         * Never called, but the return type is used by cib to determine what the
         * abstract interface is.
         */
        AbstractInterface * base() const;

        template<typename BuilderValue>
        static constexpr auto build() {
            auto constexpr handler_builder = BuilderValue::value;
            auto constexpr config = handler_builder.get_num_callbacks();
            return handler_builder.template internal_build<config>();
        }
    };

    template<
        typename BaseMsgT,
        typename... ExtraCallbackArgsT>
    auto callback =
        [](auto name, auto match_msg, auto... callbacks) ->
            callback_impl<
                BaseMsgT,
                extra_callback_args<ExtraCallbackArgsT...>,
                decltype(name),
                decltype(match_msg),
                callback_types<decltype(callbacks)...>
            >
        {
            return {match_msg, callbacks...};
        };
}