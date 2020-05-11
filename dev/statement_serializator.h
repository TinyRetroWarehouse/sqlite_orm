#pragma once

#include <sstream>  //  std::stringstream
#include <string>  //  std::string
#include <type_traits>  //  std::is_arithmetic, std::enable_if
#include <vector>  //  std::vector
#include <algorithm>  //  std::iter_swap

#include "core_functions.h"
#include "constraints.h"
#include "conditions.h"
#include "column.h"
#include "rowid.h"
#include "type_printer.h"
#include "table_name_collector.h"
#include "column_names_getter.h"
#include "order_by_serializator.h"

namespace sqlite_orm {

    namespace internal {

        template<class T, class SFINAE = void>
        struct statement_serializator;

        template<class T, class C>
        std::string serialize(const T &t, const C &context) {
            statement_serializator<T> serializator;
            return serializator(t, context);
        }

        template<class T>
        struct statement_serializator<T, typename std::enable_if<is_bindable<T>::value>::type> {
            using statement_type = T;

            template<class C>
            std::string operator()(const statement_type &statement, const C &context) {
                if(context.replace_bindable_with_question) {
                    return "?";
                } else {
                    return field_printer<T>{}(statement);
                }
            }
        };

        template<class T>
        struct statement_serializator<std::reference_wrapper<T>, void> {
            using statement_type = std::reference_wrapper<T>;

            template<class C>
            std::string operator()(const statement_type &s, const C &context) {
                return serialize(s.get(), context);
            }
        };

        template<>
        struct statement_serializator<std::nullptr_t, void> {
            using statement_type = std::nullptr_t;

            template<class C>
            std::string operator()(const statement_type &, const C &) {
                return "?";
            }
        };

        template<class T>
        struct statement_serializator<alias_holder<T>, void> {
            using statement_type = alias_holder<T>;

            template<class C>
            std::string operator()(const statement_type &, const C &) {
                return T::get();
            }
        };

        template<class R, class S, class... Args>
        struct statement_serializator<core_function_t<R, S, Args...>, void> {
            using statement_type = core_function_t<R, S, Args...>;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                std::stringstream ss;
                ss << static_cast<std::string>(c) << "(";
                std::vector<std::string> args;
                using args_type = typename std::decay<decltype(c)>::type::args_type;
                args.reserve(std::tuple_size<args_type>::value);
                iterate_tuple(c.args, [&args, &context](auto &v) {
                    args.push_back(serialize(v, context));
                });
                for(size_t i = 0; i < args.size(); ++i) {
                    ss << args[i];
                    if(i < args.size() - 1) {
                        ss << ", ";
                    }
                }
                ss << ")";
                return ss.str();
            }
        };

        template<class T, class E>
        struct statement_serializator<as_t<T, E>, void> {
            using statement_type = as_t<T, E>;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                auto tableAliasString = alias_extractor<T>::get();
                return serialize(c.expression, context) + " AS " + tableAliasString;
            }
        };

        template<class T, class P>
        struct statement_serializator<alias_column_t<T, P>, void> {
            using statement_type = alias_column_t<T, P>;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                std::stringstream ss;
                if(!context.skip_table_name) {
                    ss << "'" << T::get() << "'.";
                }
                auto newContext = context;
                newContext.skip_table_name = true;
                ss << serialize(c.column, newContext);
                return ss.str();
            }
        };

        template<>
        struct statement_serializator<std::string, void> {
            using statement_type = std::string;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                if(context.replace_bindable_with_question) {
                    return "?";
                } else {
                    return "\"" + c + "\"";
                }
            }
        };

        template<>
        struct statement_serializator<const char *, void> {
            using statement_type = const char *;

            template<class C>
            std::string operator()(const char *c, const C &context) const {
                if(context.replace_bindable_with_question) {
                    return "?";
                } else {
                    return std::string("'") + c + "'";
                }
            }
        };

        template<class O, class F>
        struct statement_serializator<F O::*, void> {
            using statement_type = F O::*;

            template<class C>
            std::string operator()(const statement_type &m, const C &context) const {
                std::stringstream ss;
                if(!context.skip_table_name) {
                    ss << "\"" << context.impl.find_table_name(typeid(O)) << "\".";
                }
                ss << "\"" << context.column_name(m) << "\"";
                return ss.str();
            }
        };

        template<>
        struct statement_serializator<rowid_t, void> {
            using statement_type = rowid_t;

            template<class C>
            std::string operator()(const statement_type &s, const C &) {
                return static_cast<std::string>(s);
            }
        };

        template<>
        struct statement_serializator<oid_t, void> {
            using statement_type = oid_t;

            template<class C>
            std::string operator()(const statement_type &s, const C &) {
                return static_cast<std::string>(s);
            }
        };

        template<>
        struct statement_serializator<_rowid_t, void> {
            using statement_type = _rowid_t;

            template<class C>
            std::string operator()(const statement_type &s, const C &) {
                return static_cast<std::string>(s);
            }
        };

        template<class O>
        struct statement_serializator<table_rowid_t<O>, void> {
            using statement_type = table_rowid_t<O>;

            template<class C>
            std::string operator()(const statement_type &s, const C &context) {
                std::stringstream ss;
                if(!context.skip_table_name) {
                    ss << "'" << context.impl.find_table_name(typeid(O)) << "'.";
                }
                ss << static_cast<std::string>(s);
                return ss.str();
            }
        };

        template<class O>
        struct statement_serializator<table_oid_t<O>, void> {
            using statement_type = table_oid_t<O>;

            template<class C>
            std::string operator()(const statement_type &s, const C &context) {
                std::stringstream ss;
                if(!context.skip_table_name) {
                    ss << "'" << context.impl.find_table_name(typeid(O)) << "'.";
                }
                ss << static_cast<std::string>(s);
                return ss.str();
            }
        };

        template<class O>
        struct statement_serializator<table__rowid_t<O>, void> {
            using statement_type = table__rowid_t<O>;

            template<class C>
            std::string operator()(const statement_type &s, const C &context) {
                std::stringstream ss;
                if(!context.skip_table_name) {
                    ss << "'" << context.impl.find_table_name(typeid(O)) << "'.";
                }
                ss << static_cast<std::string>(s);
                return ss.str();
            }
        };

        template<class L, class R, class... Ds>
        struct statement_serializator<binary_operator<L, R, Ds...>, void> {
            using statement_type = binary_operator<L, R, Ds...>;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                auto lhs = serialize(c.lhs, context);
                auto rhs = serialize(c.rhs, context);
                std::stringstream ss;
                ss << "(" << lhs << " " << static_cast<std::string>(c) << " " << rhs << ")";
                return ss.str();
            }
        };

        template<class T>
        struct statement_serializator<count_asterisk_t<T>, void> {
            using statement_type = count_asterisk_t<T>;

            template<class C>
            std::string operator()(const statement_type &, const C &context) const {
                return serialize(count_asterisk_without_type{}, context);
            }
        };

        template<>
        struct statement_serializator<count_asterisk_without_type, void> {
            using statement_type = count_asterisk_without_type;

            template<class C>
            std::string operator()(const statement_type &c, const C &) const {
                std::stringstream ss;
                ss << static_cast<std::string>(c) << "(*)";
                return ss.str();
            }
        };

        template<class T>
        struct statement_serializator<distinct_t<T>, void> {
            using statement_type = distinct_t<T>;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                std::stringstream ss;
                auto expr = serialize(c.t, context);
                ss << static_cast<std::string>(c) << "(" << expr << ")";
                return ss.str();
            }
        };

        template<class T>
        struct statement_serializator<all_t<T>, void> {
            using statement_type = all_t<T>;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                std::stringstream ss;
                auto expr = serialize(c.t, context);
                ss << static_cast<std::string>(c) << "(" << expr << ")";
                return ss.str();
            }
        };

        template<class T, class F>
        struct statement_serializator<column_pointer<T, F>, void> {
            using statement_type = column_pointer<T, F>;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                std::stringstream ss;
                if(!context.skip_table_name) {
                    ss << "'" << context.impl.find_table_name(typeid(T)) << "'.";
                }
                ss << "\"" << context.impl.column_name_simple(c.field) << "\"";
                return ss.str();
            }
        };

        template<class T, class E>
        struct statement_serializator<cast_t<T, E>, void> {
            using statement_type = cast_t<T, E>;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                std::stringstream ss;
                ss << static_cast<std::string>(c) << " (";
                ss << serialize(c.expression, context) << " AS " << type_printer<T>().print() << ")";
                return ss.str();
            }
        };

        template<class T>
        struct statement_serializator<T,
                                      typename std::enable_if<is_base_of_template<T, compound_operator>::value>::type> {
            using statement_type = T;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                std::stringstream ss;
                ss << serialize(c.left, context) << " ";
                ss << static_cast<std::string>(c) << " ";
                ss << serialize(c.right, context);
                return ss.str();
            }
        };

        template<class R, class T, class E, class... Args>
        struct statement_serializator<simple_case_t<R, T, E, Args...>, void> {
            using statement_type = simple_case_t<R, T, E, Args...>;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                std::stringstream ss;
                ss << "CASE ";
                c.case_expression.apply([&ss, context](auto &c) {
                    ss << serialize(c, context) << " ";
                });
                iterate_tuple(c.args, [&ss, context](auto &pair) {
                    ss << "WHEN " << serialize(pair.first, context) << " ";
                    ss << "THEN " << serialize(pair.second, context) << " ";
                });
                c.else_expression.apply([&ss, context](auto &el) {
                    ss << "ELSE " << serialize(el, context) << " ";
                });
                ss << "END";
                return ss.str();
            }
        };

        template<class T>
        struct statement_serializator<is_null_t<T>, void> {
            using statement_type = is_null_t<T>;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                std::stringstream ss;
                ss << serialize(c.t, context) << " " << static_cast<std::string>(c);
                return ss.str();
            }
        };

        template<class T>
        struct statement_serializator<is_not_null_t<T>, void> {
            using statement_type = is_not_null_t<T>;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                std::stringstream ss;
                ss << serialize(c.t, context) << " " << static_cast<std::string>(c);
                return ss.str();
            }
        };

        template<class T>
        struct statement_serializator<bitwise_not_t<T>, void> {
            using statement_type = bitwise_not_t<T>;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                std::stringstream ss;
                ss << static_cast<std::string>(c) << " ";
                auto cString = serialize(c.argument, context);
                ss << " (" << cString << " )";
                return ss.str();
            }
        };

        template<class T>
        struct statement_serializator<negated_condition_t<T>, void> {
            using statement_type = negated_condition_t<T>;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                std::stringstream ss;
                ss << static_cast<std::string>(c) << " ";
                auto cString = serialize(c.c, context);
                ss << " (" << cString << " )";
                return ss.str();
            }
        };

        template<class T>
        struct statement_serializator<T,
                                      typename std::enable_if<is_base_of_template<T, binary_condition>::value>::type> {
            using statement_type = T;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                auto leftString = serialize(c.l, context);
                auto rightString = serialize(c.r, context);
                std::stringstream ss;
                if(context.use_parentheses) {
                    ss << "(";
                }
                ss << leftString << " " << static_cast<std::string>(c) << " " << rightString;
                if(context.use_parentheses) {
                    ss << ")";
                }
                return ss.str();
            }
        };

        template<class T>
        struct statement_serializator<named_collate<T>, void> {
            using statement_type = named_collate<T>;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                auto newContext = context;
                newContext.use_parentheses = false;
                auto res = serialize(c.expr, newContext);
                return res + " " + static_cast<std::string>(c);
            }
        };

        template<class T>
        struct statement_serializator<collate_t<T>, void> {
            using statement_type = collate_t<T>;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                auto newContext = context;
                newContext.use_parentheses = false;
                auto res = serialize(c.expr, newContext);
                return res + " " + static_cast<std::string>(c);
            }
        };

        template<class L, class A>
        struct statement_serializator<in_t<L, A>, void> {
            using statement_type = in_t<L, A>;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                std::stringstream ss;
                auto leftString = serialize(c.l, context);
                ss << leftString << " " << static_cast<std::string>(c) << " ";
                ss << serialize(c.arg, context);
                return ss.str();
            }
        };

        template<class L, class E>
        struct statement_serializator<in_t<L, std::vector<E>>, void> {
            using statement_type = in_t<L, std::vector<E>>;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                std::stringstream ss;
                auto leftString = serialize(c.l, context);
                ss << leftString << " " << static_cast<std::string>(c) << " ( ";
                for(size_t index = 0; index < c.arg.size(); ++index) {
                    auto &value = c.arg[index];
                    ss << " " << serialize(value, context);
                    if(index < c.arg.size() - 1) {
                        ss << ", ";
                    }
                }
                ss << " )";
                return ss.str();
            }
        };

        template<class A, class T, class E>
        struct statement_serializator<like_t<A, T, E>, void> {
            using statement_type = like_t<A, T, E>;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                std::stringstream ss;
                ss << serialize(c.arg, context) << " ";
                ss << static_cast<std::string>(c) << " ";
                ss << serialize(c.pattern, context);
                c.arg3.apply([&ss, &context](auto &value) {
                    ss << " ESCAPE " << serialize(value, context);
                });
                return ss.str();
            }
        };

        template<class A, class T>
        struct statement_serializator<glob_t<A, T>, void> {
            using statement_type = glob_t<A, T>;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                std::stringstream ss;
                ss << serialize(c.arg, context) << " ";
                ss << static_cast<std::string>(c) << " ";
                ss << serialize(c.pattern, context);
                return ss.str();
            }
        };

        template<class A, class T>
        struct statement_serializator<between_t<A, T>, void> {
            using statement_type = between_t<A, T>;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                std::stringstream ss;
                auto expr = serialize(c.expr, context);
                ss << expr << " " << static_cast<std::string>(c) << " ";
                ss << serialize(c.b1, context);
                ss << " AND ";
                ss << serialize(c.b2, context);
                return ss.str();
            }
        };

        template<class T>
        struct statement_serializator<exists_t<T>, void> {
            using statement_type = exists_t<T>;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                std::stringstream ss;
                ss << static_cast<std::string>(c) << " ";
                ss << serialize(c.t, context);
                return ss.str();
            }
        };

        template<>
        struct statement_serializator<constraints::autoincrement_t, void> {
            using statement_type = constraints::autoincrement_t;

            template<class C>
            std::string operator()(const statement_type &c, const C &) const {
                return static_cast<std::string>(c);
            }
        };

        template<class... Cs>
        struct statement_serializator<constraints::primary_key_t<Cs...>, void> {
            using statement_type = constraints::primary_key_t<Cs...>;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                auto res = static_cast<std::string>(c);
                using columns_tuple = typename statement_type::columns_tuple;
                auto columnsCount = std::tuple_size<columns_tuple>::value;
                if(columnsCount) {
                    res += "(";
                    decltype(columnsCount) columnIndex = 0;
                    iterate_tuple(c.columns, [&context, &res, &columnIndex, columnsCount](auto &column) {
                        res += context.column_name(column);
                        if(columnIndex < columnsCount - 1) {
                            res += ", ";
                        }
                        ++columnIndex;
                    });
                    res += ")";
                }
                return res;
            }
        };

        template<>
        struct statement_serializator<constraints::unique_t, void> {
            using statement_type = constraints::unique_t;

            template<class C>
            std::string operator()(const statement_type &c, const C &) const {
                return static_cast<std::string>(c);
            }
        };

        template<>
        struct statement_serializator<constraints::collate_t, void> {
            using statement_type = constraints::collate_t;

            template<class C>
            std::string operator()(const statement_type &c, const C &) const {
                return static_cast<std::string>(c);
            }
        };

        template<class T>
        struct statement_serializator<constraints::default_t<T>, void> {
            using statement_type = constraints::default_t<T>;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                return static_cast<std::string>(c) + " (" + serialize(c.value, context) + ")";
            }
        };

        template<class... Cs, class... Rs>
        struct statement_serializator<constraints::foreign_key_t<std::tuple<Cs...>, std::tuple<Rs...>>, void> {
            using statement_type = constraints::foreign_key_t<std::tuple<Cs...>, std::tuple<Rs...>>;

            template<class C>
            std::string operator()(const statement_type &fk, const C &context) const {
                std::stringstream ss;
                std::vector<std::string> columnNames;
                using columns_type_t = typename std::decay<decltype(fk)>::type::columns_type;
                constexpr const size_t columnsCount = std::tuple_size<columns_type_t>::value;
                columnNames.reserve(columnsCount);
                iterate_tuple(fk.columns, [&columnNames, &context](auto &v) {
                    columnNames.push_back(context.impl.column_name(v));
                });
                ss << "FOREIGN KEY(";
                for(size_t i = 0; i < columnNames.size(); ++i) {
                    ss << "'" << columnNames[i] << "'";
                    if(i < columnNames.size() - 1) {
                        ss << ", ";
                    }
                }
                ss << ") REFERENCES ";
                std::vector<std::string> referencesNames;
                using references_type_t = typename std::decay<decltype(fk)>::type::references_type;
                constexpr const size_t referencesCount = std::tuple_size<references_type_t>::value;
                referencesNames.reserve(referencesCount);
                {
                    using first_reference_t = typename std::tuple_element<0, references_type_t>::type;
                    using first_reference_mapped_type = typename internal::table_type<first_reference_t>::type;
                    auto refTableName = context.impl.find_table_name(typeid(first_reference_mapped_type));
                    ss << '\'' << refTableName << '\'';
                }
                iterate_tuple(fk.references, [&referencesNames, &context](auto &v) {
                    referencesNames.push_back(context.impl.column_name(v));
                });
                ss << "(";
                for(size_t i = 0; i < referencesNames.size(); ++i) {
                    ss << "'" << referencesNames[i] << "'";
                    if(i < referencesNames.size() - 1) {
                        ss << ", ";
                    }
                }
                ss << ")";
                if(fk.on_update) {
                    ss << ' ' << static_cast<std::string>(fk.on_update) << " " << fk.on_update._action;
                }
                if(fk.on_delete) {
                    ss << ' ' << static_cast<std::string>(fk.on_delete) << " " << fk.on_delete._action;
                }
                return ss.str();
            }
        };

        template<class T>
        struct statement_serializator<constraints::check_t<T>, void> {
            using statement_type = constraints::check_t<T>;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                return static_cast<std::string>(c) + " " + serialize(c.expression, context);
            }
        };

        template<class O, class T, class G, class S, class... Op>
        struct statement_serializator<column_t<O, T, G, S, Op...>, void> {
            using statement_type = column_t<O, T, G, S, Op...>;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                std::stringstream ss;
                ss << "'" << c.name << "' ";
                using column_type = typename std::decay<decltype(c)>::type;
                using field_type = typename column_type::field_type;
                using constraints_type = typename column_type::constraints_type;
                ss << type_printer<field_type>().print() << " ";
                {
                    std::vector<std::string> constraintsStrings;
                    constexpr const size_t constraintsCount = std::tuple_size<constraints_type>::value;
                    constraintsStrings.reserve(constraintsCount);
                    int primaryKeyIndex = -1;
                    int autoincrementIndex = -1;
                    int tupleIndex = 0;
                    iterate_tuple(
                        c.constraints,
                        [&constraintsStrings, &primaryKeyIndex, &autoincrementIndex, &tupleIndex, &context](auto &v) {
                            using constraint_type = typename std::decay<decltype(v)>::type;
                            constraintsStrings.push_back(serialize(v, context));
                            if(is_primary_key<constraint_type>::value) {
                                primaryKeyIndex = tupleIndex;
                            } else if(std::is_same<constraints::autoincrement_t, constraint_type>::value) {
                                autoincrementIndex = tupleIndex;
                            }
                            ++tupleIndex;
                        });
                    if(primaryKeyIndex != -1 && autoincrementIndex != -1 && autoincrementIndex < primaryKeyIndex) {
                        iter_swap(constraintsStrings.begin() + primaryKeyIndex,
                                  constraintsStrings.begin() + autoincrementIndex);
                    }
                    for(auto &str: constraintsStrings) {
                        ss << str << ' ';
                    }
                }
                if(c.not_null()) {
                    ss << "NOT NULL ";
                }
                return ss.str();
            }
        };

        template<class T, class... Args>
        struct statement_serializator<remove_all_t<T, Args...>, void> {
            using statement_type = remove_all_t<T, Args...>;

            template<class C>
            std::string operator()(const statement_type &rem, const C &context) const {
                auto &tImpl = context.impl.template get_impl<T>();
                std::stringstream ss;
                ss << "DELETE FROM '" << tImpl.table.name << "' ";
                iterate_tuple(rem.conditions, [&context, &ss](auto &v) {
                    ss << serialize(v, context);
                });
                return ss.str();
            }
        };

        template<class T>
        struct statement_serializator<insert_t<T>, void> {
            using statement_type = insert_t<T>;

            template<class C>
            std::string operator()(const statement_type &, const C &context) const {
                using object_type = typename expression_object_type<statement_type>::type;
                auto &tImpl = context.impl.template get_impl<object_type>();
                std::stringstream ss;
                ss << "INSERT INTO '" << tImpl.table.name << "' ";
                std::vector<std::string> columnNames;
                auto compositeKeyColumnNames = tImpl.table.composite_key_columns_names();

                tImpl.table.for_each_column([&tImpl, &columnNames, &compositeKeyColumnNames](auto &c) {
                    if(tImpl.table._without_rowid || !c.template has<constraints::primary_key_t<>>()) {
                        auto it = find(compositeKeyColumnNames.begin(), compositeKeyColumnNames.end(), c.name);
                        if(it == compositeKeyColumnNames.end()) {
                            columnNames.emplace_back(c.name);
                        }
                    }
                });

                auto columnNamesCount = columnNames.size();
                if(columnNamesCount) {
                    ss << "(";
                    for(size_t i = 0; i < columnNamesCount; ++i) {
                        ss << "\"" << columnNames[i] << "\"";
                        if(i < columnNamesCount - 1) {
                            ss << ",";
                        } else {
                            ss << ")";
                        }
                        ss << " ";
                    }
                } else {
                    ss << "DEFAULT ";
                }
                ss << "VALUES ";
                if(columnNamesCount) {
                    ss << "(";
                    for(size_t i = 0; i < columnNamesCount; ++i) {
                        ss << "?";
                        if(i < columnNamesCount - 1) {
                            ss << ", ";
                        } else {
                            ss << ")";
                        }
                    }
                }
                return ss.str();
            }
        };

        template<class T, class... Ids>
        struct statement_serializator<remove_t<T, Ids...>, void> {
            using statement_type = remove_t<T, Ids...>;

            template<class C>
            std::string operator()(const statement_type &, const C &context) const {
                auto &tImpl = context.impl.template get_impl<T>();
                std::stringstream ss;
                ss << "DELETE FROM '" << tImpl.table.name << "' ";
                ss << "WHERE ";
                auto primaryKeyColumnNames = tImpl.table.primary_key_column_names();
                for(size_t i = 0; i < primaryKeyColumnNames.size(); ++i) {
                    ss << "\"" << primaryKeyColumnNames[i] << "\""
                       << " = ? ";
                    if(i < primaryKeyColumnNames.size() - 1) {
                        ss << "AND ";
                    }
                }
                return ss.str();
            }
        };

        template<class It>
        struct statement_serializator<insert_range_t<It>, void> {
            using statement_type = insert_range_t<It>;

            template<class C>
            std::string operator()(const statement_type &statement, const C &context) const {
                using expression_type = typename std::decay<decltype(statement)>::type;
                using object_type = typename expression_type::object_type;
                auto &tImpl = context.impl.template get_impl<object_type>();

                std::stringstream ss;
                ss << "INSERT INTO '" << tImpl.table.name << "' (";
                std::vector<std::string> columnNames;
                tImpl.table.for_each_column([&columnNames](auto &c) {
                    if(!c.template has<constraints::primary_key_t<>>()) {
                        columnNames.emplace_back(c.name);
                    }
                });

                auto columnNamesCount = columnNames.size();
                for(size_t i = 0; i < columnNamesCount; ++i) {
                    ss << "\"" << columnNames[i] << "\"";
                    if(i < columnNamesCount - 1) {
                        ss << ",";
                    } else {
                        ss << ")";
                    }
                    ss << " ";
                }
                ss << "VALUES ";
                auto valuesString = [columnNamesCount] {
                    std::stringstream ss;
                    ss << "(";
                    for(size_t i = 0; i < columnNamesCount; ++i) {
                        ss << "?";
                        if(i < columnNamesCount - 1) {
                            ss << ", ";
                        } else {
                            ss << ")";
                        }
                    }
                    return ss.str();
                }();
                auto valuesCount = static_cast<int>(std::distance(statement.range.first, statement.range.second));
                for(auto i = 0; i < valuesCount; ++i) {
                    ss << valuesString;
                    if(i < valuesCount - 1) {
                        ss << ",";
                    }
                    ss << " ";
                }
                return ss.str();
            }
        };

        template<class T, class... Args>
        struct statement_serializator<select_t<T, Args...>, void> {
            using statement_type = select_t<T, Args...>;

            template<class C>
            std::string operator()(const statement_type &sel, const C &context) const {
                std::stringstream ss;
                if(!is_base_of_template<T, compound_operator>::value) {
                    if(!sel.highest_level) {
                        ss << "( ";
                    }
                    ss << "SELECT ";
                }
                if(get_distinct(sel.col)) {
                    ss << static_cast<std::string>(distinct(0)) << " ";
                }
                auto columnNames = get_column_names(sel.col, context);
                for(size_t i = 0; i < columnNames.size(); ++i) {
                    ss << columnNames[i];
                    if(i < columnNames.size() - 1) {
                        ss << ",";
                    }
                    ss << " ";
                }
                table_name_collector collector{[&context](std::type_index ti) {
                    return context.impl.find_table_name(ti);
                }};
                iterate_ast(sel.col, collector);
                iterate_ast(sel.conditions, collector);
                internal::join_iterator<Args...>()([&collector, &context](const auto &c) {
                    using original_join_type = typename std::decay<decltype(c)>::type::join_type::type;
                    using cross_join_type = typename internal::mapped_type_proxy<original_join_type>::type;
                    auto crossJoinedTableName = context.impl.find_table_name(typeid(cross_join_type));
                    auto tableAliasString = alias_extractor<original_join_type>::get();
                    std::pair<std::string, std::string> tableNameWithAlias(std::move(crossJoinedTableName),
                                                                           std::move(tableAliasString));
                    collector.table_names.erase(tableNameWithAlias);
                });
                if(!collector.table_names.empty()) {
                    ss << "FROM ";
                    std::vector<std::pair<std::string, std::string>> tableNames(collector.table_names.begin(),
                                                                                collector.table_names.end());
                    for(size_t i = 0; i < tableNames.size(); ++i) {
                        auto &tableNamePair = tableNames[i];
                        ss << "'" << tableNamePair.first << "' ";
                        if(!tableNamePair.second.empty()) {
                            ss << tableNamePair.second << " ";
                        }
                        if(int(i) < int(tableNames.size()) - 1) {
                            ss << ",";
                        }
                        ss << " ";
                    }
                }
                iterate_tuple(sel.conditions, [&context, &ss](auto &v) {
                    ss << serialize(v, context);
                });
                if(!is_base_of_template<T, compound_operator>::value) {
                    if(!sel.highest_level) {
                        ss << ") ";
                    }
                }
                return ss.str();
            }
        };

        template<class T>
        struct statement_serializator<where_t<T>, void> {
            using statement_type = where_t<T>;

            template<class C>
            std::string operator()(const statement_type &w, const C &context) const {
                std::stringstream ss;
                ss << static_cast<std::string>(w) << " ";
                auto whereString = serialize(w.c, context);
                ss << "( " << whereString << ") ";
                return ss.str();
            }
        };

        template<class O>
        struct statement_serializator<order_by_t<O>, void> {
            using statement_type = order_by_t<O>;

            template<class C>
            std::string operator()(const statement_type &orderBy, const C &context) const {
                std::stringstream ss;
                ss << static_cast<std::string>(orderBy) << " ";
                auto orderByString = serialize_order_by(orderBy, context);
                ss << orderByString << " ";
                return ss.str();
            }
        };

        template<class C>
        struct statement_serializator<dynamic_order_by_t<C>, void> {
            using statement_type = dynamic_order_by_t<C>;

            template<class CC>
            std::string operator()(const statement_type &orderBy, const CC &context) const {
                return serialize_order_by(orderBy, context);
            }
        };

        template<class... Args>
        struct statement_serializator<multi_order_by_t<Args...>, void> {
            using statement_type = multi_order_by_t<Args...>;

            template<class C>
            std::string operator()(const statement_type &orderBy, const C &context) const {
                std::stringstream ss;
                std::vector<std::string> expressions;
                iterate_tuple(orderBy.args, [&expressions, &context](auto &v) {
                    auto expression = serialize_order_by(v, context);
                    expressions.push_back(move(expression));
                });
                ss << static_cast<std::string>(orderBy) << " ";
                for(size_t i = 0; i < expressions.size(); ++i) {
                    ss << expressions[i];
                    if(i < expressions.size() - 1) {
                        ss << ", ";
                    }
                }
                ss << " ";
                return ss.str();
            }
        };

        template<class O>
        struct statement_serializator<cross_join_t<O>, void> {
            using statement_type = cross_join_t<O>;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                std::stringstream ss;
                ss << static_cast<std::string>(c) << " ";
                ss << " '" << context.impl.find_table_name(typeid(O)) << "'";
                return ss.str();
            }
        };

        template<class T, class O>
        struct statement_serializator<inner_join_t<T, O>, void> {
            using statement_type = inner_join_t<T, O>;

            template<class C>
            std::string operator()(const statement_type &l, const C &context) const {
                std::stringstream ss;
                ss << static_cast<std::string>(l) << " ";
                auto aliasString = alias_extractor<T>::get();
                ss << " '" << context.impl.find_table_name(typeid(typename mapped_type_proxy<T>::type)) << "' ";
                if(aliasString.length()) {
                    ss << "'" << aliasString << "' ";
                }
                ss << serialize(l.constraint, context);
                return ss.str();
            }
        };

        template<class T>
        struct statement_serializator<on_t<T>, void> {
            using statement_type = on_t<T>;

            template<class C>
            std::string operator()(const statement_type &t, const C &context) const {
                std::stringstream ss;
                auto newContext = context;
                newContext.skip_table_name = false;
                ss << static_cast<std::string>(t) << " " << serialize(t.arg, newContext) << " ";
                return ss.str();
            }
        };

        template<class T, class O>
        struct statement_serializator<join_t<T, O>, void> {
            using statement_type = join_t<T, O>;

            template<class C>
            std::string operator()(const statement_type &l, const C &context) const {
                std::stringstream ss;
                ss << static_cast<std::string>(l) << " ";
                ss << " '" << context.impl.find_table_name(typeid(T)) << "' ";
                ss << serialize(l.constraint, context);
                return ss.str();
            }
        };

        template<class T, class O>
        struct statement_serializator<left_join_t<T, O>, void> {
            using statement_type = left_join_t<T, O>;

            template<class C>
            std::string operator()(const statement_type &l, const C &context) const {
                std::stringstream ss;
                ss << static_cast<std::string>(l) << " ";
                ss << " '" << context.impl.find_table_name(typeid(T)) << "' ";
                ss << serialize(l.constraint, context);
                return ss.str();
            }
        };

        template<class T, class O>
        struct statement_serializator<left_outer_join_t<T, O>, void> {
            using statement_type = left_outer_join_t<T, O>;

            template<class C>
            std::string operator()(const statement_type &l, const C &context) const {
                std::stringstream ss;
                ss << static_cast<std::string>(l) << " ";
                ss << " '" << context.impl.find_table_name(typeid(T)) << "' ";
                ss << serialize(l.constraint, context);
                return ss.str();
            }
        };

        template<class O>
        struct statement_serializator<natural_join_t<O>, void> {
            using statement_type = natural_join_t<O>;

            template<class C>
            std::string operator()(const statement_type &c, const C &context) const {
                std::stringstream ss;
                ss << static_cast<std::string>(c) << " ";
                ss << " '" << context.impl.find_table_name(typeid(O)) << "'";
                return ss.str();
            }
        };

        template<class... Args>
        struct statement_serializator<group_by_t<Args...>, void> {
            using statement_type = group_by_t<Args...>;

            template<class C>
            std::string operator()(const statement_type &groupBy, const C &context) const {
                std::stringstream ss;
                std::vector<std::string> expressions;
                auto newContext = context;
                newContext.skip_table_name = false;
                iterate_tuple(groupBy.args, [&expressions, &newContext](auto &v) {
                    auto expression = serialize(v, newContext);
                    expressions.push_back(expression);
                });
                ss << static_cast<std::string>(groupBy) << " ";
                for(size_t i = 0; i < expressions.size(); ++i) {
                    ss << expressions[i];
                    if(i < expressions.size() - 1) {
                        ss << ", ";
                    }
                }
                ss << " ";
                return ss.str();
            }
        };

        template<class T>
        struct statement_serializator<having_t<T>, void> {
            using statement_type = having_t<T>;

            template<class C>
            std::string operator()(const statement_type &hav, const C &context) const {
                std::stringstream ss;
                auto newContext = context;
                newContext.skip_table_name = false;
                ss << static_cast<std::string>(hav) << " ";
                ss << serialize(hav.t, newContext) << " ";
                return ss.str();
            }
        };

        /**
         *  HO - has offset
         *  OI - offset is implicit
         */
        template<class T, bool HO, bool OI, class O>
        struct statement_serializator<limit_t<T, HO, OI, O>, void> {
            using statement_type = limit_t<T, HO, OI, O>;

            template<class C>
            std::string operator()(const statement_type &limt, const C &context) const {
                auto newContext = context;
                newContext.skip_table_name = false;
                std::stringstream ss;
                ss << static_cast<std::string>(limt) << " ";
                if(HO) {
                    if(OI) {
                        limt.off.apply([&newContext, &ss](auto &value) {
                            ss << serialize(value, newContext);
                        });
                        ss << ", ";
                        ss << serialize(limt.lim, newContext);
                    } else {
                        ss << serialize(limt.lim, newContext) << " OFFSET ";
                        limt.off.apply([&newContext, &ss](auto &value) {
                            ss << serialize(value, newContext);
                        });
                    }
                } else {
                    ss << serialize(limt.lim, newContext);
                }
                return ss.str();
            }
        };

        template<class F, class O>
        struct statement_serializator<using_t<F, O>, void> {
            using statement_type = using_t<F, O>;

            template<class C>
            std::string operator()(const statement_type &statement, const C &context) const {
                auto newContext = context;
                newContext.skip_table_name = true;
                return static_cast<std::string>(statement) + " (" + serialize(statement.column, newContext) + " )";
            }
        };
    }
}
