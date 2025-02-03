/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _THREAD_SAFETY_H_
#define _THREAD_SAFETY_H_

/* See also https://clang.llvm.org/docs/ThreadSafetyAnalysis.html */

/*
 * Enable thread safety attributes only for clang. The attributes can be safely
 * ignored when compiling with other compilers.
 */
#if defined(__clang__)
#define THREAD_ANNOTATION_ATTRIBUTE_(...) __attribute__((__VA_ARGS__))
#else
#define THREAD_ANNOTATION_ATTRIBUTE_(...)
#endif

/*
 * Macro for applying a capability as an attribute to a type definition.
 * This macro can be used in struct definitions and also in typedefs.
 * @x must be a string.
 */
#define CAPABILITY(x) THREAD_ANNOTATION_ATTRIBUTE_(capability(x))

/*
 * Macro for defining a capability name that is not tied to an existing type.
 * @capability_name is declared as an external variable. Any attempt to
 * read or modify that external variable will result in a linker error.
 */
#define DEFINE_CAPABILITY(capability_name)	      \
	extern const struct {} CAPABILITY(#capability_name) capability_name

/*
 * Attribute for structure members that declares that the structure members are
 * protected by the given capability.
 */
#define GUARDED_BY(x) THREAD_ANNOTATION_ATTRIBUTE_(guarded_by(x))

/*
 * Attribute for pointer structure members that declares that the contents
 * of these pointers are protected by the given capability.
 */
#define PT_GUARDED_BY(x) THREAD_ANNOTATION_ATTRIBUTE_(pt_guarded_by(x))

/*
 * Attribute for instances of data structures that declares that the given
 * capabilities must be acquired before the annotated data structure.
 */
#define ACQUIRED_BEFORE(...)					\
	THREAD_ANNOTATION_ATTRIBUTE_(acquired_before(__VA_ARGS__))

/*
 * Attribute for instances of data structures that declares that the given
 * capabilities must be acquired after the annotated data structure.
 */
#define ACQUIRED_AFTER(...)					\
	THREAD_ANNOTATION_ATTRIBUTE_(acquired_after(__VA_ARGS__))

/*
 * Function attribute that declares that the caller must have exclusive access
 * to the given capabilities.
 */
#define REQUIRES(...)							\
	THREAD_ANNOTATION_ATTRIBUTE_(requires_capability(__VA_ARGS__))

/*
 * Function attribute that declares that the caller must have shared access
 * to the given capabilities.
 */
#define REQUIRES_SHARED(...)						\
	THREAD_ANNOTATION_ATTRIBUTE_(requires_shared_capability(__VA_ARGS__))

/*
 * Function attribute that declares that the function acquires the given
 * capability.
 */
#define ACQUIRE(...)						\
	THREAD_ANNOTATION_ATTRIBUTE_(acquire_capability(__VA_ARGS__))

/*
 * Function attribute that declares that the function acquires the given
 * shared capability.
 */
#define ACQUIRE_SHARED(...)						\
	THREAD_ANNOTATION_ATTRIBUTE_(acquire_shared_capability(__VA_ARGS__))

/*
 * Function attribute that declares that the function releases the given
 * capability.
 */
#define RELEASE(...)						\
	THREAD_ANNOTATION_ATTRIBUTE_(release_capability(__VA_ARGS__))

/*
 * Function attribute that declares that the function releases the given
 * shared capability.
 */
#define RELEASE_SHARED(...)						\
	THREAD_ANNOTATION_ATTRIBUTE_(release_shared_capability(__VA_ARGS__))

/*
 * Function attribute that declares that a function only acquires the given
 * capability (2nd argument) for a given return value (first argument).
 */
#define TRY_ACQUIRE(...)						\
	THREAD_ANNOTATION_ATTRIBUTE_(try_acquire_capability(__VA_ARGS__))

/*
 * Function attribute that declares that a function only acquires the given
 * shared capability (2nd argument) for a given return value (first argument).
 */
#define TRY_ACQUIRE_SHARED(...)						\
	THREAD_ANNOTATION_ATTRIBUTE_(try_acquire_shared_capability(__VA_ARGS__))

/*
 * Function attribute that declares that the caller must not hold the given
 * capabilities.
 */
#define EXCLUDES(...) THREAD_ANNOTATION_ATTRIBUTE_(locks_excluded(__VA_ARGS__))

/*
 * Tell the compiler that the given capability is held.
 */
#define ASSERT_CAPABILITY(x) THREAD_ANNOTATION_ATTRIBUTE_(assert_capability(x))

/*
 * Tell the compiler that the given shared capability is held.
 */
#define ASSERT_SHARED_CAPABILITY(x)				\
	THREAD_ANNOTATION_ATTRIBUTE_(assert_shared_capability(x))

/*
 * Function attribute that declares that a function returns a pointer to a
 * capability.
 */
#define RETURN_CAPABILITY(x) THREAD_ANNOTATION_ATTRIBUTE_(lock_returned(x))

/* Function attribute that disables thread-safety analysis. */
#define NO_THREAD_SAFETY_ANALYSIS				\
	THREAD_ANNOTATION_ATTRIBUTE_(no_thread_safety_analysis)

#endif /* _THREAD_SAFETY_H_ */
