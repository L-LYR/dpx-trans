#pragma once

#include <tuple>
#include <variant>

#include "concepts/rpc.hxx"

namespace dpx::trans {

namespace details {
template <typename Head, typename>
struct Cons;

template <typename Head, typename... Tail>
struct Cons<Head, std::tuple<Tail...>> {
  using type = std::tuple<Head, Tail...>;
};

template <template <typename T> class Pred, typename...>
struct Filter;

template <template <typename T> class Pred>
struct Filter<Pred> {
  using type = std::tuple<>;
};

template <template <typename T> class Pred, typename Head, typename... Tail>
struct Filter<Pred, Head, Tail...> {
  using type = std::conditional_t<Pred<Head>::value, typename Cons<Head, typename Filter<Pred, Tail...>::type>::type,
                                  typename Filter<Pred, Tail...>::type>;
};

}  // namespace details

template <Rpc... rpcs>
struct OnewayRpcFilter {
  template <Rpc rpc>
  struct Predictor {
    static constexpr bool value = std::is_void_v<resp_t<rpc>>;
  };

  using type = details::Filter<Predictor, rpcs...>::type;
};

template <Rpc... rpcs>
struct NormalRpcFilter {
  template <Rpc rpc>
  struct Predictor {
    static constexpr bool value = !std::is_void_v<resp_t<rpc>>;
  };

  using type = details::Filter<Predictor, rpcs...>::type;
};

namespace details {

template <typename... T>
std::variant<std::monostate, T...> f(std::tuple<T...>) {}

template <typename T>
using to_variant = decltype(f(std::declval<T>()));

template <Rpc... rpcs>
std::tuple<typename rpcs::Handler...> handlers(std::tuple<rpcs...>) {}

template <typename T>
using to_handlers = decltype(handlers(std::declval<T>()));

}  // namespace details

template <typename T>
struct GeneralHandler {
  using type = details::to_variant<details::to_handlers<T>>;
};

template <typename T>
using general_handler_t = GeneralHandler<T>::type;

}  // namespace dpx::trans
