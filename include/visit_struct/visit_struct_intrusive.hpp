//  (C) Copyright 2015 - 2016 Christopher Beck

//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef VISIT_STRUCT_INTRUSIVE_HPP_INCLUDED
#define VISIT_STRUCT_INTRUSIVE_HPP_INCLUDED

/***
 * A collection of templates and macros supporting a second form of VISIT_STRUCT
 * mechanism.
 *
 * In this version, the visitable members are declared *within* the body of the
 * struct, at the same time that they are actually declared.
 *
 * This version uses templates for iteration rather than macros, so it's really
 * fairly different. It is more DRY and less likely to produce gross error
 * messages than the other, at the cost of being invasive to your structure
 * definition.
 *
 * This version adds some typedefs to your class, and it invisibly adds some
 * declarations of obscure member functions to your class. These declarations
 * do not have corresponding definitions and generate no object code, they are
 * merely a device for metaprogramming, exploiting overload resolution rules to
 * create "state". In normal code, you won't be able to detect any of this.
 *
 * This sounds a lot more evil than it really is -- it is morally equivalent to
 * `std::declval`, I would say, which is also specified to be a declaration with
 * no definition, which you simply aren't permitted to odr-use.
 */

#include <visit_struct/visit_struct.hpp>

namespace visit_struct {

namespace detail {

/***
 * Poor man's mpl vector
 */

template <class... Ts>
struct TypeList {
  static constexpr unsigned int size = sizeof...(Ts);
};

// Append metafunction
template <class List, class T>
struct Append;

template <class... Ts, class T>
struct Append<TypeList<Ts...>, T> {
  typedef TypeList<Ts..., T> type;
};

template<class L, class T>
using Append_t = typename Append<L, T>::type;

/***
 * The "rank" template is a trick which can be used for
 * certain metaprogramming techniques. It creates
 * an inheritance hierarchy of trivial classes.
 */

template <int N>
struct Rank : Rank<N - 1> {};

template <>
struct Rank<0> {};

static constexpr int maxVisitableRank = 200;

/***
 * To create a "compile-time" TypeList whose members are accumulated one-by-one,
 * the basic idea is to define a function, which takes a `Rank` object, and
 * whose return type is the type representing the current value of the list.
 *
 * That function is not a template function -- it is defined as taking a
 * particular rank object. Initially, it is defined only for `Rank<0>`.
 *
 * To add an element to the list, we define an overload of the function, which
 * takes the next higher `Rank` as it's argument. It's return value is,
 * the new value of the list, formed by using `Append_t` with the old value.
 *
 * To obtain the current value of the list, we use decltype with the name of the
 * function, and `Rank<200>`, or some suitably large integer. The C++ standard
 * specifies that overload resolution is in this case unambiguous and must
 * select the overload for the "most-derived" type which matches.
 *
 * The upshot is that `decltype(my_function(Rank<200>{}))` is a single well-formed
 * expression, which, because of C++ overload resolution rules, can be a
 * "mutable" value from the point of view of metaprogramming.
 *
 *
 * Attribution:
 * I first learned this trick from a stackoverflow post by Roman Perepelitsa:
 * http://stackoverflow.com/questions/4790721/c-type-registration-at-compile-time-trick
 *
 * He attributes it to a talk from Matt Calabrese at BoostCon 2011.
 *
 *
 * The expression is inherently dangerous if you are using it inside the body
 * of a struct -- obviously, it has different values at different points of the
 * structure definition. The "END_VISITABLES" macro is important in that this
 * finalizes the list, typedeffing `decltype(my_function(Rank<200>{}))` to some
 * fixed name in your struct at a specific point in the definition. That
 * typedef can only ultimately have one meaning, no matter where else the name
 * may be used (even implicitly) in your structure definition. That typedef is
 * what the trait defined in this header ultimately hooks into to find the
 * visitable members.
 */

// A tag inserted into a structure to mark it as visitable

struct intrusive_tag{};

/***
 * Helper structures which perform pack expansion in order to visit a structure.
 */

// In MSVC 2015, a pointer to member cannot be constexpr, however it can be a
// template parameter. This is a workaround.

template <typename S, typename T, T S::*member_ptr>
struct member_ptr_helper {
  static constexpr T & apply(S & s) { return s.*member_ptr; }
  static constexpr const T & apply(const S & s) { return s.*member_ptr; }
  static constexpr T && apply(S && s) { return std::move(s.*member_ptr); }
};

template <typename M>
struct member_helper {
  template <typename V, typename S>
  VISIT_STRUCT_CONSTEXPR static void apply_visitor(V && visitor, S && structure_instance) {
    visitor(M::member_name, M::apply(std::forward<S>(structure_instance)));
  }
};

template <typename Mlist>
struct structure_helper;

template <typename... Ms>
struct structure_helper<TypeList<Ms...>> {
  template <typename V, typename S>
  VISIT_STRUCT_CONSTEXPR static void apply_visitor(V && visitor, S && structure_instance) {
    // Use parameter pack expansion to force evaluation of the member helper for each member in the list.
    // Inside parens, a comma operator is being used to discard the void value and produce an integer, while
    // not being an unevaluated context and having the order of evaluation be enforced by the compiler.
    // Extra zero at the end is to avoid UB for having a zero-size array.
    int dummy[] = { (member_helper<Ms>::apply_visitor(std::forward<V>(visitor), std::forward<S>(structure_instance)), 0)..., 0};
    // Suppress unused warnings, even in case of empty parameter pack
    static_cast<void>(dummy);
    static_cast<void>(visitor);
    static_cast<void>(structure_instance);
  }
};


} // end namespace detail


/***
 * Implement trait
 */

namespace traits {

template <typename T>
struct visitable <T,
                  typename std::enable_if<
                               std::is_same<typename T::Visit_Struct_Visitable_Structure_Tag__,
                                            ::visit_struct::detail::intrusive_tag
                                           >::value
                                         >::type
                 >
{
  template <typename V, typename S>
  VISIT_STRUCT_CONSTEXPR static void apply(V && v, S && s) {
    detail::structure_helper<typename T::Visit_Struct_Registered_Members_List__>::apply_visitor(std::forward<V>(v), std::forward<S>(s));
  }

  static constexpr bool value = true;
};

} // end namespace trait

} // end namespace visit_struct

// Macros to be used within a structure definition

#define VISIT_STRUCT_GET_REGISTERED_MEMBERS decltype(Visit_Struct_Get_Visitables__(::visit_struct::detail::Rank<visit_struct::detail::maxVisitableRank>{}))

#define VISIT_STRUCT_MAKE_MEMBER_NAME(NAME) Visit_Struct_Member_Record__##NAME

#define BEGIN_VISITABLES(NAME)                                                                                   \
typedef NAME VISIT_STRUCT_CURRENT_TYPE;                                                                          \
::visit_struct::detail::TypeList<> static inline Visit_Struct_Get_Visitables__(::visit_struct::detail::Rank<0>); \
static_assert(true, "")

#define VISITABLE(TYPE, NAME)                                                                                    \
TYPE NAME;                                                                                                       \
struct VISIT_STRUCT_MAKE_MEMBER_NAME(NAME) :                                                                     \
  visit_struct::detail::member_ptr_helper<VISIT_STRUCT_CURRENT_TYPE,                                             \
                                          TYPE,                                                                  \
                                          &VISIT_STRUCT_CURRENT_TYPE::NAME>                                      \
{                                                                                                                \
  static constexpr const char * const member_name = #NAME;                                                       \
};                                                                                                               \
static inline ::visit_struct::detail::Append_t<VISIT_STRUCT_GET_REGISTERED_MEMBERS,                              \
                                               VISIT_STRUCT_MAKE_MEMBER_NAME(NAME)>                              \
  Visit_Struct_Get_Visitables__(::visit_struct::detail::Rank<VISIT_STRUCT_GET_REGISTERED_MEMBERS::size + 1>);    \
static_assert(true, "")

#define END_VISITABLES                                                                                           \
typedef VISIT_STRUCT_GET_REGISTERED_MEMBERS Visit_Struct_Registered_Members_List__;                              \
typedef ::visit_struct::detail::intrusive_tag Visit_Struct_Visitable_Structure_Tag__;                            \
static_assert(true, "")


#endif // VISIT_STRUCT_INTRUSIVE_HPP_INCLUDED
