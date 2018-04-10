/*
 * main.c
 * main() routine, printer functions
 *
 * Copyright (c) 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018
 *     pkgconf authors (see AUTHORS).
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * This software is provided 'as is' and without any warranty, express or
 * implied.  In no event shall the authors be liable for any damages arising
 * from the use of this software.
 */

#include <libpkgconf/stdinc.h>
#include <libpkgconf/libpkgconf.h>
#include "libpkgconf/config.h"
#include "getopt_long.h"
#include "renderer-msvc.h"
#ifdef _WIN32
#include <io.h>     /* for _setmode() */
#include <fcntl.h>
#endif

#define PKG_CFLAGS_ONLY_I		(((uint64_t) 1) << 2)
#define PKG_CFLAGS_ONLY_OTHER		(((uint64_t) 1) << 3)
#define PKG_CFLAGS			(PKG_CFLAGS_ONLY_I|PKG_CFLAGS_ONLY_OTHER)
#define PKG_LIBS_ONLY_LDPATH		(((uint64_t) 1) << 5)
#define PKG_LIBS_ONLY_LIBNAME		(((uint64_t) 1) << 6)
#define PKG_LIBS_ONLY_OTHER		(((uint64_t) 1) << 7)
#define PKG_LIBS			(PKG_LIBS_ONLY_LDPATH|PKG_LIBS_ONLY_LIBNAME|PKG_LIBS_ONLY_OTHER)
#define PKG_MODVERSION			(((uint64_t) 1) << 8)
#define PKG_REQUIRES			(((uint64_t) 1) << 9)
#define PKG_REQUIRES_PRIVATE		(((uint64_t) 1) << 10)
#define PKG_VARIABLES			(((uint64_t) 1) << 11)
#define PKG_DIGRAPH			(((uint64_t) 1) << 12)
#define PKG_KEEP_SYSTEM_CFLAGS		(((uint64_t) 1) << 13)
#define PKG_KEEP_SYSTEM_LIBS		(((uint64_t) 1) << 14)
#define PKG_VERSION			(((uint64_t) 1) << 15)
#define PKG_ABOUT			(((uint64_t) 1) << 16)
#define PKG_ENV_ONLY			(((uint64_t) 1) << 17)
#define PKG_ERRORS_ON_STDOUT		(((uint64_t) 1) << 18)
#define PKG_SILENCE_ERRORS		(((uint64_t) 1) << 19)
#define PKG_IGNORE_CONFLICTS		(((uint64_t) 1) << 20)
#define PKG_STATIC			(((uint64_t) 1) << 21)
#define PKG_NO_UNINSTALLED		(((uint64_t) 1) << 22)
#define PKG_UNINSTALLED			(((uint64_t) 1) << 23)
#define PKG_LIST			(((uint64_t) 1) << 24)
#define PKG_HELP			(((uint64_t) 1) << 25)
#define PKG_PRINT_ERRORS		(((uint64_t) 1) << 26)
#define PKG_SIMULATE			(((uint64_t) 1) << 27)
#define PKG_NO_CACHE			(((uint64_t) 1) << 28)
#define PKG_PROVIDES			(((uint64_t) 1) << 29)
#define PKG_VALIDATE			(((uint64_t) 1) << 30)
#define PKG_LIST_PACKAGE_NAMES		(((uint64_t) 1) << 31)
#define PKG_NO_PROVIDES			(((uint64_t) 1) << 32)
#define PKG_PURE			(((uint64_t) 1) << 33)
#define PKG_PATH			(((uint64_t) 1) << 34)
#define PKG_DEFINE_PREFIX		(((uint64_t) 1) << 35)
#define PKG_DONT_DEFINE_PREFIX		(((uint64_t) 1) << 36)
#define PKG_DONT_RELOCATE_PATHS		(((uint64_t) 1) << 37)
#define PKG_DEBUG			(((uint64_t) 1) << 38)
#define PKG_SHORT_ERRORS		(((uint64_t) 1) << 39)
#define PKG_EXISTS			(((uint64_t) 1) << 40)
#define PKG_MSVC_SYNTAX			(((uint64_t) 1) << 41)
#define PKG_INTERNAL_CFLAGS		(((uint64_t) 1) << 42)

static pkgconf_client_t pkg_client;
static const pkgconf_fragment_render_ops_t *want_render_ops = NULL;

static uint64_t want_flags;
static int maximum_traverse_depth = 2000;
static size_t maximum_package_count = 0;

static char *want_variable = NULL;
static char *want_fragment_filter = NULL;

FILE *error_msgout = NULL;
FILE *logfile_out = NULL;

static bool
error_handler(const char *msg, const pkgconf_client_t *client, const void *data)
{
	(void) client;
	(void) data;
	fprintf(error_msgout, "%s", msg);
	return true;
}

static bool
print_list_entry(const pkgconf_pkg_t *entry, void *data)
{
	(void) data;

	if (entry->flags & PKGCONF_PKG_PROPF_UNINSTALLED)
		return false;

	printf("%-30s %s - %s\n", entry->id, entry->realname, entry->description);

	return false;
}

static bool
print_package_entry(const pkgconf_pkg_t *entry, void *data)
{
	(void) data;

	if (entry->flags & PKGCONF_PKG_PROPF_UNINSTALLED)
		return false;

	printf("%s\n", entry->id);

	return false;
}

static bool
filter_cflags(const pkgconf_client_t *client, const pkgconf_fragment_t *frag, void *data)
{
	int got_flags = 0;
	(void) client;
	(void) data;

	if (!(want_flags & PKG_KEEP_SYSTEM_CFLAGS) && pkgconf_fragment_has_system_dir(client, frag))
		return false;

	if (want_fragment_filter != NULL && (strchr(want_fragment_filter, frag->type) == NULL || !frag->type))
		return false;

	if (frag->type == 'I')
		got_flags = PKG_CFLAGS_ONLY_I;
	else
		got_flags = PKG_CFLAGS_ONLY_OTHER;

	return (want_flags & got_flags) != 0;
}

static bool
filter_libs(const pkgconf_client_t *client, const pkgconf_fragment_t *frag, void *data)
{
	int got_flags = 0;
	(void) client;
	(void) data;

	if (!(want_flags & PKG_KEEP_SYSTEM_LIBS) && pkgconf_fragment_has_system_dir(client, frag))
		return false;

	if (want_fragment_filter != NULL && (strchr(want_fragment_filter, frag->type) == NULL || !frag->type))
		return false;

	switch (frag->type)
	{
		case 'L': got_flags = PKG_LIBS_ONLY_LDPATH; break;
		case 'l': got_flags = PKG_LIBS_ONLY_LIBNAME; break;
		default: got_flags = PKG_LIBS_ONLY_OTHER; break;
	}

	return (want_flags & got_flags) != 0;
}

static void
print_variables(pkgconf_pkg_t *pkg)
{
	pkgconf_node_t *node;

	PKGCONF_FOREACH_LIST_ENTRY(pkg->vars.head, node)
	{
		pkgconf_tuple_t *tuple = node->data;

		printf("%s\n", tuple->key);
	}
}

static void
print_requires(pkgconf_pkg_t *pkg)
{
	pkgconf_node_t *node;

	PKGCONF_FOREACH_LIST_ENTRY(pkg->required.head, node)
	{
		pkgconf_dependency_t *dep = node->data;

		printf("%s", dep->package);

		if (dep->version != NULL)
			printf(" %s %s", pkgconf_pkg_get_comparator(dep), dep->version);

		printf("\n");
	}
}

static void
print_requires_private(pkgconf_pkg_t *pkg)
{
	pkgconf_node_t *node;

	PKGCONF_FOREACH_LIST_ENTRY(pkg->requires_private.head, node)
	{
		pkgconf_dependency_t *dep = node->data;

		printf("%s", dep->package);

		if (dep->version != NULL)
			printf(" %s %s", pkgconf_pkg_get_comparator(dep), dep->version);

		printf("\n");
	}
}

static void
print_provides(pkgconf_pkg_t *pkg)
{
	pkgconf_node_t *node;

	PKGCONF_FOREACH_LIST_ENTRY(pkg->provides.head, node)
	{
		pkgconf_dependency_t *dep = node->data;

		printf("%s", dep->package);

		if (dep->version != NULL)
			printf(" %s %s", pkgconf_pkg_get_comparator(dep), dep->version);

		printf("\n");
	}
}

static bool
apply_provides(pkgconf_client_t *client, pkgconf_pkg_t *world, void *unused, int maxdepth)
{
	pkgconf_node_t *iter;
	(void) client;
	(void) unused;
	(void) maxdepth;

	PKGCONF_FOREACH_LIST_ENTRY(world->required.head, iter)
	{
		pkgconf_dependency_t *dep = iter->data;
		pkgconf_pkg_t *pkg = dep->match;

		print_provides(pkg);
	}

	return true;
}

static void
print_digraph_node(pkgconf_client_t *client, pkgconf_pkg_t *pkg, void *unused)
{
	pkgconf_node_t *node;
	(void) client;
	(void) unused;

	printf("\"%s\" [fontname=Sans fontsize=8]\n", pkg->id);

	PKGCONF_FOREACH_LIST_ENTRY(pkg->required.head, node)
	{
		pkgconf_dependency_t *dep = node->data;

		printf("\"%s\" -- \"%s\" [fontname=Sans fontsize=8]\n", dep->package, pkg->id);
	}
}

static bool
apply_digraph(pkgconf_client_t *client, pkgconf_pkg_t *world, void *unused, int maxdepth)
{
	int eflag;

	printf("graph deptree {\n");
	printf("edge [color=blue len=7.5 fontname=Sans fontsize=8]\n");
	printf("node [fontname=Sans fontsize=8]\n");

	eflag = pkgconf_pkg_traverse(client, world, print_digraph_node, unused, maxdepth, 0);

	if (eflag != PKGCONF_PKG_ERRF_OK)
		return false;

	printf("}\n");
	return true;
}

static bool
apply_modversion(pkgconf_client_t *client, pkgconf_pkg_t *world, void *unused, int maxdepth)
{
	pkgconf_node_t *iter;
	(void) client;
	(void) unused;
	(void) maxdepth;

	PKGCONF_FOREACH_LIST_ENTRY(world->required.head, iter)
	{
		pkgconf_dependency_t *dep = iter->data;
		pkgconf_pkg_t *pkg = dep->match;

		if (pkg->version != NULL)
			printf("%s\n", pkg->version);
	}

	return true;
}

static bool
apply_variables(pkgconf_client_t *client, pkgconf_pkg_t *world, void *unused, int maxdepth)
{
	pkgconf_node_t *iter;
	(void) client;
	(void) unused;
	(void) maxdepth;

	PKGCONF_FOREACH_LIST_ENTRY(world->required.head, iter)
	{
		pkgconf_dependency_t *dep = iter->data;
		pkgconf_pkg_t *pkg = dep->match;

		print_variables(pkg);
	}

	return true;
}

static bool
apply_path(pkgconf_client_t *client, pkgconf_pkg_t *world, void *unused, int maxdepth)
{
	pkgconf_node_t *iter;
	(void) client;
	(void) unused;
	(void) maxdepth;

	PKGCONF_FOREACH_LIST_ENTRY(world->required.head, iter)
	{
		pkgconf_dependency_t *dep = iter->data;
		pkgconf_pkg_t *pkg = dep->match;

		/* a module entry with no filename is either virtual, static (builtin) or synthesized. */
		if (pkg->filename != NULL)
			printf("%s\n", pkg->filename);
	}

	return true;
}

static void
print_variable(pkgconf_client_t *client, pkgconf_pkg_t *pkg, const char *variable)
{
	const char *var;

	var = pkgconf_tuple_find(client, &pkg->vars, variable);
	if (var != NULL)
		printf("%s", var);
}

static bool
apply_variable(pkgconf_client_t *client, pkgconf_pkg_t *world, void *variable, int maxdepth)
{
	pkgconf_node_t *iter;
	(void) maxdepth;

	PKGCONF_FOREACH_LIST_ENTRY(world->required.head, iter)
	{
		pkgconf_dependency_t *dep = iter->data;
		pkgconf_pkg_t *pkg = dep->match;

		if (iter->prev != NULL)
			printf(" ");

		print_variable(client, pkg, variable);
	}

	printf("\n");

	return true;
}

static bool
apply_env_var(const char *prefix, pkgconf_client_t *client, pkgconf_pkg_t *world, int maxdepth,
	unsigned int (*collect_fn)(pkgconf_client_t *client, pkgconf_pkg_t *world, pkgconf_list_t *list, int maxdepth),
	bool (*filter_fn)(const pkgconf_client_t *client, const pkgconf_fragment_t *frag, void *data))
{
	pkgconf_list_t unfiltered_list = PKGCONF_LIST_INITIALIZER;
	pkgconf_list_t filtered_list = PKGCONF_LIST_INITIALIZER;
	unsigned int eflag;
	char *render_buf;

	eflag = collect_fn(client, world, &unfiltered_list, maxdepth);
	if (eflag != PKGCONF_PKG_ERRF_OK)
		return false;

	pkgconf_fragment_filter(client, &filtered_list, &unfiltered_list, filter_fn, NULL);

	if (filtered_list.head == NULL)
		goto out;

	render_buf = pkgconf_fragment_render(&filtered_list, true, want_render_ops);
	printf("%s='%s'\n", prefix, render_buf);
	free(render_buf);

out:
	pkgconf_fragment_free(&unfiltered_list);
	pkgconf_fragment_free(&filtered_list);

	return true;
}

static bool
apply_env(pkgconf_client_t *client, pkgconf_pkg_t *world, void *env_prefix_p, int maxdepth)
{
	const char *want_env_prefix = env_prefix_p, *it;
	char workbuf[PKGCONF_ITEM_SIZE];

	for (it = want_env_prefix; *it != '\0'; it++)
		if (!isalpha(*it) && !isdigit(*it))
			return false;

	snprintf(workbuf, sizeof workbuf, "%s_CFLAGS", want_env_prefix);
	if (!apply_env_var(workbuf, client, world, maxdepth, pkgconf_pkg_cflags, filter_cflags))
		return false;

	snprintf(workbuf, sizeof workbuf, "%s_LIBS", want_env_prefix);
	if (!apply_env_var(workbuf, client, world, maxdepth, pkgconf_pkg_libs, filter_libs))
		return false;

	return true;
}

static bool
apply_cflags(pkgconf_client_t *client, pkgconf_pkg_t *world, void *unused, int maxdepth)
{
	pkgconf_list_t unfiltered_list = PKGCONF_LIST_INITIALIZER;
	pkgconf_list_t filtered_list = PKGCONF_LIST_INITIALIZER;
	int eflag;
	char *render_buf;
	(void) unused;

	eflag = pkgconf_pkg_cflags(client, world, &unfiltered_list, maxdepth);
	if (eflag != PKGCONF_PKG_ERRF_OK)
		return false;

	pkgconf_fragment_filter(client, &filtered_list, &unfiltered_list, filter_cflags, NULL);

	if (filtered_list.head == NULL)
		goto out;

	render_buf = pkgconf_fragment_render(&filtered_list, true, want_render_ops);
	printf("%s", render_buf);
	free(render_buf);

out:
	pkgconf_fragment_free(&unfiltered_list);
	pkgconf_fragment_free(&filtered_list);

	return true;
}

static bool
apply_libs(pkgconf_client_t *client, pkgconf_pkg_t *world, void *unused, int maxdepth)
{
	pkgconf_list_t unfiltered_list = PKGCONF_LIST_INITIALIZER;
	pkgconf_list_t filtered_list = PKGCONF_LIST_INITIALIZER;
	int eflag;
	char *render_buf;
	(void) unused;

	eflag = pkgconf_pkg_libs(client, world, &unfiltered_list, maxdepth);
	if (eflag != PKGCONF_PKG_ERRF_OK)
		return false;

	pkgconf_fragment_filter(client, &filtered_list, &unfiltered_list, filter_libs, NULL);

	if (filtered_list.head == NULL)
		goto out;

	render_buf = pkgconf_fragment_render(&filtered_list, true, want_render_ops);
	printf("%s", render_buf);
	free(render_buf);

out:
	pkgconf_fragment_free(&unfiltered_list);
	pkgconf_fragment_free(&filtered_list);

	return true;
}

static bool
apply_requires(pkgconf_client_t *client, pkgconf_pkg_t *world, void *unused, int maxdepth)
{
	pkgconf_node_t *iter;
	(void) client;
	(void) unused;
	(void) maxdepth;

	PKGCONF_FOREACH_LIST_ENTRY(world->required.head, iter)
	{
		pkgconf_dependency_t *dep = iter->data;
		pkgconf_pkg_t *pkg = dep->match;

		print_requires(pkg);
	}

	return true;
}

static bool
apply_requires_private(pkgconf_client_t *client, pkgconf_pkg_t *world, void *unused, int maxdepth)
{
	pkgconf_node_t *iter;
	(void) client;
	(void) unused;
	(void) maxdepth;

	PKGCONF_FOREACH_LIST_ENTRY(world->required.head, iter)
	{
		pkgconf_dependency_t *dep = iter->data;
		pkgconf_pkg_t *pkg = dep->match;

		print_requires_private(pkg);
	}
	return true;
}

static void
check_uninstalled(pkgconf_client_t *client, pkgconf_pkg_t *pkg, void *data)
{
	int *retval = data;
	(void) client;

	if (pkg->flags & PKGCONF_PKG_PROPF_UNINSTALLED)
		*retval = EXIT_SUCCESS;
}

static bool
apply_uninstalled(pkgconf_client_t *client, pkgconf_pkg_t *world, void *data, int maxdepth)
{
	int eflag;

	eflag = pkgconf_pkg_traverse(client, world, check_uninstalled, data, maxdepth, 0);

	if (eflag != PKGCONF_PKG_ERRF_OK)
		return false;

	return true;
}

static void
print_graph_node(pkgconf_client_t *client, pkgconf_pkg_t *pkg, void *data)
{
	pkgconf_node_t *n;

	(void) client;
	(void) data;

	printf("node '%s' {\n", pkg->id);

	if (pkg->version != NULL)
		printf("    version = '%s';\n", pkg->version);

	PKGCONF_FOREACH_LIST_ENTRY(pkg->required.head, n)
	{
		pkgconf_dependency_t *dep = n->data;
		printf("    dependency '%s'", dep->package);
		if (dep->compare != PKGCONF_CMP_ANY)
		{
			printf(" {\n");
			printf("        comparator = '%s';\n", pkgconf_pkg_get_comparator(dep));
			printf("        version = '%s';\n", dep->version);
			printf("    };\n");
		}
		else
			printf(";\n");
	}

	printf("};\n");
}

static bool
apply_simulate(pkgconf_client_t *client, pkgconf_pkg_t *world, void *data, int maxdepth)
{
	int eflag;

	eflag = pkgconf_pkg_traverse(client, world, print_graph_node, data, maxdepth, 0);

	if (eflag != PKGCONF_PKG_ERRF_OK)
		return false;

	return true;
}

static void
version(void)
{
	printf("%s\n", PACKAGE_VERSION);
}

static void
about(void)
{
	printf("%s %s\n", PACKAGE_NAME, PACKAGE_VERSION);
	printf("Copyright (c) 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018\n");
	printf("    pkgconf authors (see AUTHORS in documentation directory).\n\n");
	printf("Permission to use, copy, modify, and/or distribute this software for any\n");
	printf("purpose with or without fee is hereby granted, provided that the above\n");
	printf("copyright notice and this permission notice appear in all copies.\n\n");
	printf("This software is provided 'as is' and without any warranty, express or\n");
	printf("implied.  In no event shall the authors be liable for any damages arising\n");
	printf("from the use of this software.\n\n");
	printf("Report bugs at <%s>.\n", PACKAGE_BUGREPORT);
}

static void
usage(void)
{
	printf("usage: %s [OPTIONS] [LIBRARIES]\n", PACKAGE_NAME);

	printf("\nbasic options:\n\n");

	printf("  --help                            this message\n");
	printf("  --about                           print pkgconf version and license to stdout\n");
	printf("  --version                         print supported pkg-config version to stdout\n");
	printf("  --atleast-pkgconfig-version       check whether or not pkgconf is compatible\n");
	printf("                                    with a specified pkg-config version\n");
	printf("  --errors-to-stdout                print all errors on stdout instead of stderr\n");
	printf("  --print-errors                    ensure all errors are printed\n");
	printf("  --short-errors                    be less verbose about some errors\n");
	printf("  --silence-errors                  explicitly be silent about errors\n");
	printf("  --list-all                        list all known packages\n");
	printf("  --list-package-names              list all known package names\n");
	printf("  --simulate                        simulate walking the calculated dependency graph\n");
	printf("  --no-cache                        do not cache already seen packages when\n");
	printf("                                    walking the dependency graph\n");
	printf("  --log-file=filename               write an audit log to a specified file\n");
	printf("  --with-path=path                  adds a directory to the search path\n");
	printf("  --define-prefix                   override the prefix variable with one that is guessed based on\n");
	printf("                                    the location of the .pc file\n");
	printf("  --dont-define-prefix              do not override the prefix variable under any circumstances\n");
	printf("  --prefix-variable=varname         sets the name of the variable that pkgconf considers\n");
	printf("                                    to be the package prefix\n");
	printf("  --relocate=path                   relocates a path and exits (mostly for testsuite)\n");
	printf("  --dont-relocate-paths             disables path relocation support\n");

	printf("\nchecking specific pkg-config database entries:\n\n");

	printf("  --atleast-version                 require a specific version of a module\n");
	printf("  --exact-version                   require an exact version of a module\n");
	printf("  --max-version                     require a maximum version of a module\n");
	printf("  --exists                          check whether or not a module exists\n");
	printf("  --uninstalled                     check whether or not an uninstalled module will be used\n");
	printf("  --no-uninstalled                  never use uninstalled modules when satisfying dependencies\n");
	printf("  --no-provides                     do not use 'provides' rules to resolve dependencies\n");
	printf("  --maximum-traverse-depth          maximum allowed depth for dependency graph\n");
	printf("  --static                          be more aggressive when computing dependency graph\n");
	printf("                                    (for static linking)\n");
	printf("  --pure                            optimize a static dependency graph as if it were a normal\n");
	printf("                                    dependency graph\n");
	printf("  --env-only                        look only for package entries in PKG_CONFIG_PATH\n");
	printf("  --ignore-conflicts                ignore 'conflicts' rules in modules\n");
	printf("  --validate                        validate specific .pc files for correctness\n");

	printf("\nquerying specific pkg-config database fields:\n\n");

	printf("  --define-variable=varname=value   define variable 'varname' as 'value'\n");
	printf("  --variable=varname                print specified variable entry to stdout\n");
	printf("  --cflags                          print required CFLAGS to stdout\n");
	printf("  --cflags-only-I                   print required include-dir CFLAGS to stdout\n");
	printf("  --cflags-only-other               print required non-include-dir CFLAGS to stdout\n");
	printf("  --libs                            print required linker flags to stdout\n");
	printf("  --libs-only-L                     print required LDPATH linker flags to stdout\n");
	printf("  --libs-only-l                     print required LIBNAME linker flags to stdout\n");
	printf("  --libs-only-other                 print required other linker flags to stdout\n");
	printf("  --print-requires                  print required dependency frameworks to stdout\n");
	printf("  --print-requires-private          print required dependency frameworks for static\n");
	printf("                                    linking to stdout\n");
	printf("  --print-provides                  print provided dependencies to stdout\n");
	printf("  --print-variables                 print all known variables in module to stdout\n");
	printf("  --digraph                         print entire dependency graph in graphviz 'dot' format\n");
	printf("  --keep-system-cflags              keep -I%s entries in cflags output\n", SYSTEM_INCLUDEDIR);
	printf("  --keep-system-libs                keep -L%s entries in libs output\n", SYSTEM_LIBDIR);
	printf("  --path                            show the exact filenames for any matching .pc files\n");
	printf("  --modversion                      print the specified module's version to stdout\n");
	printf("  --internal-cflags                 do not filter 'internal' cflags from output\n");

	printf("\nfiltering output:\n\n");
	printf("  --msvc-syntax                     print translatable fragments in MSVC syntax\n");
	printf("  --fragment-filter=types           filter output fragments to the specified types\n");

	printf("\nreport bugs to <%s>.\n", PACKAGE_BUGREPORT);
}

static void
relocate_path(const char *path)
{
	char buf[PKGCONF_BUFSIZE];

	pkgconf_strlcpy(buf, path, sizeof buf);
	pkgconf_path_relocate(buf, sizeof buf);

	printf("%s\n", buf);
}

int
main(int argc, char *argv[])
{
	int ret;
	pkgconf_list_t pkgq = PKGCONF_LIST_INITIALIZER;
	char *builddir;
	char *sysroot_dir;
	char *env_traverse_depth;
	char *required_pkgconfig_version = NULL;
	char *required_exact_module_version = NULL;
	char *required_max_module_version = NULL;
	char *required_module_version = NULL;
	char *logfile_arg = NULL;
	char *want_env_prefix = NULL;
	unsigned int want_client_flags = PKGCONF_PKG_PKGF_NONE;

	want_flags = 0;

#ifdef _WIN32
	/* When running regression tests in cygwin, and building native
	 * executable, tests fail unless native executable outputs unix
	 * line endings.  Come to think of it, this will probably help
	 * real people who use cygwin build environments but native pkgconf, too.
	 */
	_setmode(fileno(stdout), O_BINARY);
	_setmode(fileno(stderr), O_BINARY);
#endif

	struct pkg_option options[] = {
		{ "version", no_argument, &want_flags, PKG_VERSION|PKG_PRINT_ERRORS, },
		{ "about", no_argument, &want_flags, PKG_ABOUT|PKG_PRINT_ERRORS, },
		{ "atleast-version", required_argument, NULL, 2, },
		{ "atleast-pkgconfig-version", required_argument, NULL, 3, },
		{ "libs", no_argument, &want_flags, PKG_LIBS|PKG_PRINT_ERRORS, },
		{ "cflags", no_argument, &want_flags, PKG_CFLAGS|PKG_PRINT_ERRORS, },
		{ "modversion", no_argument, &want_flags, PKG_MODVERSION|PKG_PRINT_ERRORS, },
		{ "variable", required_argument, NULL, 7, },
		{ "exists", no_argument, &want_flags, PKG_EXISTS, },
		{ "print-errors", no_argument, &want_flags, PKG_PRINT_ERRORS, },
		{ "short-errors", no_argument, &want_flags, PKG_SHORT_ERRORS, },
		{ "maximum-traverse-depth", required_argument, NULL, 11, },
		{ "static", no_argument, &want_flags, PKG_STATIC, },
		{ "pure", no_argument, &want_flags, PKG_PURE, },
		{ "print-requires", no_argument, &want_flags, PKG_REQUIRES, },
		{ "print-variables", no_argument, &want_flags, PKG_VARIABLES|PKG_PRINT_ERRORS, },
		{ "digraph", no_argument, &want_flags, PKG_DIGRAPH, },
		{ "help", no_argument, &want_flags, PKG_HELP, },
		{ "env-only", no_argument, &want_flags, PKG_ENV_ONLY, },
		{ "print-requires-private", no_argument, &want_flags, PKG_REQUIRES_PRIVATE, },
		{ "cflags-only-I", no_argument, &want_flags, PKG_CFLAGS_ONLY_I|PKG_PRINT_ERRORS, },
		{ "cflags-only-other", no_argument, &want_flags, PKG_CFLAGS_ONLY_OTHER|PKG_PRINT_ERRORS, },
		{ "libs-only-L", no_argument, &want_flags, PKG_LIBS_ONLY_LDPATH|PKG_PRINT_ERRORS, },
		{ "libs-only-l", no_argument, &want_flags, PKG_LIBS_ONLY_LIBNAME|PKG_PRINT_ERRORS, },
		{ "libs-only-other", no_argument, &want_flags, PKG_LIBS_ONLY_OTHER|PKG_PRINT_ERRORS, },
		{ "uninstalled", no_argument, &want_flags, PKG_UNINSTALLED, },
		{ "no-uninstalled", no_argument, &want_flags, PKG_NO_UNINSTALLED, },
		{ "keep-system-cflags", no_argument, &want_flags, PKG_KEEP_SYSTEM_CFLAGS, },
		{ "keep-system-libs", no_argument, &want_flags, PKG_KEEP_SYSTEM_LIBS, },
		{ "define-variable", required_argument, NULL, 27, },
		{ "exact-version", required_argument, NULL, 28, },
		{ "max-version", required_argument, NULL, 29, },
		{ "ignore-conflicts", no_argument, &want_flags, PKG_IGNORE_CONFLICTS, },
		{ "errors-to-stdout", no_argument, &want_flags, PKG_ERRORS_ON_STDOUT, },
		{ "silence-errors", no_argument, &want_flags, PKG_SILENCE_ERRORS, },
		{ "list-all", no_argument, &want_flags, PKG_LIST|PKG_PRINT_ERRORS, },
		{ "list-package-names", no_argument, &want_flags, PKG_LIST_PACKAGE_NAMES|PKG_PRINT_ERRORS, },
		{ "simulate", no_argument, &want_flags, PKG_SIMULATE, },
		{ "no-cache", no_argument, &want_flags, PKG_NO_CACHE, },
		{ "print-provides", no_argument, &want_flags, PKG_PROVIDES, },
		{ "no-provides", no_argument, &want_flags, PKG_NO_PROVIDES, },
		{ "debug", no_argument, &want_flags, PKG_DEBUG|PKG_PRINT_ERRORS, },
		{ "validate", no_argument, &want_flags, PKG_VALIDATE|PKG_PRINT_ERRORS|PKG_ERRORS_ON_STDOUT },
		{ "log-file", required_argument, NULL, 40 },
		{ "path", no_argument, &want_flags, PKG_PATH },
		{ "with-path", required_argument, NULL, 42 },
		{ "prefix-variable", required_argument, NULL, 43 },
		{ "define-prefix", no_argument, &want_flags, PKG_DEFINE_PREFIX },
		{ "relocate", required_argument, NULL, 45 },
		{ "dont-define-prefix", no_argument, &want_flags, PKG_DONT_DEFINE_PREFIX },
		{ "dont-relocate-paths", no_argument, &want_flags, PKG_DONT_RELOCATE_PATHS },
		{ "env", required_argument, NULL, 48 },
		{ "msvc-syntax", no_argument, &want_flags, PKG_MSVC_SYNTAX },
		{ "fragment-filter", required_argument, NULL, 50 },
		{ "internal-cflags", no_argument, &want_flags, PKG_INTERNAL_CFLAGS },
		{ NULL, 0, NULL, 0 }
	};

	if (getenv("PKG_CONFIG_EARLY_TRACE"))
	{
		error_msgout = stderr;
		pkgconf_client_set_trace_handler(&pkg_client, error_handler, NULL);
	}

	pkgconf_client_init(&pkg_client, error_handler, NULL);

	while ((ret = pkg_getopt_long_only(argc, argv, "", options, NULL)) != -1)
	{
		switch (ret)
		{
		case 2:
			required_module_version = pkg_optarg;
			break;
		case 3:
			required_pkgconfig_version = pkg_optarg;
			break;
		case 7:
			want_variable = pkg_optarg;
			break;
		case 11:
			maximum_traverse_depth = atoi(pkg_optarg);
			break;
		case 27:
			pkgconf_tuple_define_global(&pkg_client, pkg_optarg);
			break;
		case 28:
			required_exact_module_version = pkg_optarg;
			break;
		case 29:
			required_max_module_version = pkg_optarg;
			break;
		case 40:
			logfile_arg = pkg_optarg;
			break;
		case 42:
			pkgconf_path_add(pkg_optarg, &pkg_client.dir_list, true);
			break;
		case 43:
			pkgconf_client_set_prefix_varname(&pkg_client, pkg_optarg);
			break;
		case 45:
			relocate_path(pkg_optarg);
			return EXIT_SUCCESS;
		case 48:
			want_env_prefix = pkg_optarg;
			break;
		case 50:
			want_fragment_filter = pkg_optarg;
			break;
		case '?':
		case ':':
			return EXIT_FAILURE;
			break;
		default:
			break;
		}
	}

	if ((want_flags & PKG_MSVC_SYNTAX) == PKG_MSVC_SYNTAX)
		want_render_ops = msvc_renderer_get();

	if ((env_traverse_depth = getenv("PKG_CONFIG_MAXIMUM_TRAVERSE_DEPTH")) != NULL)
		maximum_traverse_depth = atoi(env_traverse_depth);

	if ((want_flags & PKG_PRINT_ERRORS) != PKG_PRINT_ERRORS)
		want_flags |= (PKG_SILENCE_ERRORS);

	if ((want_flags & PKG_SILENCE_ERRORS) == PKG_SILENCE_ERRORS && !getenv("PKG_CONFIG_DEBUG_SPEW"))
		want_flags |= (PKG_SILENCE_ERRORS);
	else
		want_flags &= ~(PKG_SILENCE_ERRORS);

	if (getenv("PKG_CONFIG_DONT_RELOCATE_PATHS"))
		want_flags |= (PKG_DONT_RELOCATE_PATHS);

	if ((want_flags & PKG_VALIDATE) == PKG_VALIDATE || (want_flags & PKG_DEBUG) == PKG_DEBUG)
		pkgconf_client_set_warn_handler(&pkg_client, error_handler, NULL);

	if ((want_flags & PKG_DEBUG) == PKG_DEBUG)
		pkgconf_client_set_trace_handler(&pkg_client, error_handler, NULL);

	if ((want_flags & PKG_ABOUT) == PKG_ABOUT)
	{
		about();
		return EXIT_SUCCESS;
	}

	if ((want_flags & PKG_VERSION) == PKG_VERSION)
	{
		if (argc > 2)
		{
			fprintf(stderr, "%s: --version specified with other options or module names, assuming --modversion.\n", argv[0]);

			want_flags &= ~PKG_VERSION;
			want_flags |= PKG_MODVERSION;
		}
		else
		{
			version();
			return EXIT_SUCCESS;
		}
	}

	if ((want_flags & PKG_HELP) == PKG_HELP)
	{
		usage();
		return EXIT_SUCCESS;
	}

	if ((want_flags & PKG_SHORT_ERRORS) == PKG_SHORT_ERRORS)
		want_client_flags |= PKGCONF_PKG_PKGF_SIMPLIFY_ERRORS;

	if ((want_flags & PKG_DONT_RELOCATE_PATHS) == PKG_DONT_RELOCATE_PATHS)
		want_client_flags |= PKGCONF_PKG_PKGF_DONT_RELOCATE_PATHS;

	error_msgout = stderr;
	if ((want_flags & PKG_ERRORS_ON_STDOUT) == PKG_ERRORS_ON_STDOUT)
		error_msgout = stdout;
	if ((want_flags & PKG_SILENCE_ERRORS) == PKG_SILENCE_ERRORS)
		error_msgout = fopen(PATH_DEV_NULL, "w");

	if ((want_flags & PKG_IGNORE_CONFLICTS) == PKG_IGNORE_CONFLICTS || getenv("PKG_CONFIG_IGNORE_CONFLICTS") != NULL)
		want_client_flags |= PKGCONF_PKG_PKGF_SKIP_CONFLICTS;

	if ((want_flags & PKG_STATIC) == PKG_STATIC)
		want_client_flags |= (PKGCONF_PKG_PKGF_SEARCH_PRIVATE | PKGCONF_PKG_PKGF_MERGE_PRIVATE_FRAGMENTS);

	/* if --static and --pure are both specified, then disable merge-back.
	 * this allows for a --static which searches private modules, but has the same fragment behaviour as if
	 * --static were disabled.  see <https://github.com/pkgconf/pkgconf/issues/83> for rationale.
	 */
	if ((want_flags & PKG_PURE) == PKG_PURE || getenv("PKG_CONFIG_PURE_DEPGRAPH") != NULL)
		want_client_flags &= ~PKGCONF_PKG_PKGF_MERGE_PRIVATE_FRAGMENTS;

	if ((want_flags & PKG_ENV_ONLY) == PKG_ENV_ONLY)
		want_client_flags |= PKGCONF_PKG_PKGF_ENV_ONLY;

	if ((want_flags & PKG_NO_CACHE) == PKG_NO_CACHE)
		want_client_flags |= PKGCONF_PKG_PKGF_NO_CACHE;

/* On Windows we want to always redefine the prefix by default
 * but allow that behavior to be manually disabled */
#if !defined(_WIN32) && !defined(_WIN64)
	if ((want_flags & PKG_DEFINE_PREFIX) == PKG_DEFINE_PREFIX)
#endif
		want_client_flags |= PKGCONF_PKG_PKGF_REDEFINE_PREFIX;

	if ((want_flags & PKG_NO_UNINSTALLED) == PKG_NO_UNINSTALLED || getenv("PKG_CONFIG_DISABLE_UNINSTALLED") != NULL)
		want_client_flags |= PKGCONF_PKG_PKGF_NO_UNINSTALLED;

	if ((want_flags & PKG_NO_PROVIDES) == PKG_NO_PROVIDES)
		want_client_flags |= PKGCONF_PKG_PKGF_SKIP_PROVIDES;

	if ((want_flags & PKG_DONT_DEFINE_PREFIX) == PKG_DONT_DEFINE_PREFIX)
		want_client_flags &= ~PKGCONF_PKG_PKGF_REDEFINE_PREFIX;

	if ((want_flags & PKG_INTERNAL_CFLAGS) == PKG_INTERNAL_CFLAGS)
		want_client_flags |= PKGCONF_PKG_PKGF_DONT_FILTER_INTERNAL_CFLAGS;

#ifdef XXX_NOTYET
	/* if these selectors are used, it means that we are inquiring about a single package.
	 * so signal to libpkgconf that we do not want to use the dependency resolver for more than one level,
	 * and also limit the SAT problem to a single package.
	 *
	 * i disabled this because too many upstream maintainers are still invoking pkg-config correctly to have
	 * the more sane behaviour as default.  use --maximum-traverse-depth=1 or PKG_CONFIG_MAXIMUM_TRAVERSE_DEPTH
	 * environment variable to get the same results in meantime.
	 */
	if ((want_flags & PKG_EXISTS) == 0 &&
		((want_flags & PKG_REQUIRES) == PKG_REQUIRES ||
		(want_flags & PKG_REQUIRES_PRIVATE) == PKG_REQUIRES_PRIVATE ||
		(want_flags & PKG_PROVIDES) == PKG_PROVIDES ||
		(want_flags & PKG_VARIABLES) == PKG_VARIABLES ||
		(want_flags & PKG_MODVERSION) == PKG_MODVERSION ||
		(want_flags & PKG_PATH) == PKG_PATH ||
		want_variable != NULL))
	{
		maximum_package_count = 1;
		maximum_traverse_depth = 1;
	}
#endif

	if (getenv("PKG_CONFIG_ALLOW_SYSTEM_CFLAGS") != NULL)
		want_flags |= PKG_KEEP_SYSTEM_CFLAGS;

	if (getenv("PKG_CONFIG_ALLOW_SYSTEM_LIBS") != NULL)
		want_flags |= PKG_KEEP_SYSTEM_LIBS;

	if ((builddir = getenv("PKG_CONFIG_TOP_BUILD_DIR")) != NULL)
		pkgconf_client_set_buildroot_dir(&pkg_client, builddir);

	if ((sysroot_dir = getenv("PKG_CONFIG_SYSROOT_DIR")) != NULL)
		pkgconf_client_set_sysroot_dir(&pkg_client, sysroot_dir);

	/* we have determined what features we want most likely.  in some cases, we override later. */
	pkgconf_client_set_flags(&pkg_client, want_client_flags);

	/* at this point, want_client_flags should be set, so build the dir list */
	pkgconf_pkg_dir_list_build(&pkg_client);

	if (required_pkgconfig_version != NULL)
	{
		if (pkgconf_compare_version(PACKAGE_VERSION, required_pkgconfig_version) >= 0)
			return EXIT_SUCCESS;

		return EXIT_FAILURE;
	}

	if ((want_flags & PKG_LIST) == PKG_LIST)
	{
		pkgconf_scan_all(&pkg_client, NULL, print_list_entry);
		return EXIT_SUCCESS;
	}

	if ((want_flags & PKG_LIST_PACKAGE_NAMES) == PKG_LIST_PACKAGE_NAMES)
	{
		pkgconf_scan_all(&pkg_client, NULL, print_package_entry);
		return EXIT_SUCCESS;
	}

	if (logfile_arg == NULL)
		logfile_arg = getenv("PKG_CONFIG_LOG");

	if (logfile_arg != NULL)
	{
		logfile_out = fopen(logfile_arg, "w");
		pkgconf_audit_set_log(&pkg_client, logfile_out);
	}

	if (required_module_version != NULL)
	{
		pkgconf_pkg_t *pkg;
		pkgconf_node_t *node;
		pkgconf_list_t deplist = PKGCONF_LIST_INITIALIZER;

		while (argv[pkg_optind])
		{
			pkgconf_dependency_parse_str(&pkg_client, &deplist, argv[pkg_optind], 0);
			pkg_optind++;
		}

		PKGCONF_FOREACH_LIST_ENTRY(deplist.head, node)
		{
			pkgconf_dependency_t *pkgiter = node->data;

			pkg = pkgconf_pkg_find(&pkg_client, pkgiter->package);
			if (pkg == NULL)
			{
				if (want_flags & PKG_PRINT_ERRORS)
					pkgconf_error(&pkg_client, "Package '%s' was not found\n", pkgiter->package);
				return EXIT_FAILURE;
			}

			if (pkgconf_compare_version(pkg->version, required_module_version) >= 0)
				return EXIT_SUCCESS;
		}

		return EXIT_FAILURE;
	}
	else if (required_exact_module_version != NULL)
	{
		pkgconf_pkg_t *pkg;
		pkgconf_node_t *node;
		pkgconf_list_t deplist = PKGCONF_LIST_INITIALIZER;

		while (argv[pkg_optind])
		{
			pkgconf_dependency_parse_str(&pkg_client, &deplist, argv[pkg_optind], 0);
			pkg_optind++;
		}

		PKGCONF_FOREACH_LIST_ENTRY(deplist.head, node)
		{
			pkgconf_dependency_t *pkgiter = node->data;

			pkg = pkgconf_pkg_find(&pkg_client, pkgiter->package);
			if (pkg == NULL)
			{
				if (want_flags & PKG_PRINT_ERRORS)
					pkgconf_error(&pkg_client, "Package '%s' was not found\n", pkgiter->package);
				return EXIT_FAILURE;
			}

			if (pkgconf_compare_version(pkg->version, required_exact_module_version) == 0)
				return EXIT_SUCCESS;
		}

		return EXIT_FAILURE;
	}
	else if (required_max_module_version != NULL)
	{
		pkgconf_pkg_t *pkg;
		pkgconf_node_t *node;
		pkgconf_list_t deplist = PKGCONF_LIST_INITIALIZER;

		while (argv[pkg_optind])
		{
			pkgconf_dependency_parse_str(&pkg_client, &deplist, argv[pkg_optind], 0);
			pkg_optind++;
		}

		PKGCONF_FOREACH_LIST_ENTRY(deplist.head, node)
		{
			pkgconf_dependency_t *pkgiter = node->data;

			pkg = pkgconf_pkg_find(&pkg_client, pkgiter->package);
			if (pkg == NULL)
			{
				if (want_flags & PKG_PRINT_ERRORS)
					pkgconf_error(&pkg_client, "Package '%s' was not found\n", pkgiter->package);
				return EXIT_FAILURE;
			}

			if (pkgconf_compare_version(pkg->version, required_max_module_version) <= 0)
				return EXIT_SUCCESS;
		}

		return EXIT_FAILURE;
	}

	while (1)
	{
		const char *package = argv[pkg_optind];

		if (package == NULL)
			break;

		/* check if there is a limit to the number of packages allowed to be included, if so and we have hit
		 * the limit, stop adding packages to the queue.
		 */
		if (maximum_package_count > 0 && pkgq.length > maximum_package_count)
			break;

		while (isspace((unsigned int)package[0]))
			package++;

		/* skip empty packages */
		if (package[0] == '\0') {
			pkg_optind++;
			continue;
		}

		if (argv[pkg_optind + 1] == NULL || !PKGCONF_IS_OPERATOR_CHAR(*(argv[pkg_optind + 1])))
		{
			pkgconf_queue_push(&pkgq, package);
			pkg_optind++;
		}
		else
		{
			char packagebuf[PKGCONF_BUFSIZE];

			snprintf(packagebuf, sizeof packagebuf, "%s %s %s", package, argv[pkg_optind + 1], argv[pkg_optind + 2]);
			pkg_optind += 3;

			pkgconf_queue_push(&pkgq, packagebuf);
		}
	}

	if (pkgq.head == NULL)
	{
		fprintf(stderr, "Please specify at least one package name on the command line.\n");
		return EXIT_FAILURE;
	}

	ret = EXIT_SUCCESS;

	if ((want_flags & PKG_SIMULATE) == PKG_SIMULATE)
	{
		want_flags &= ~(PKG_CFLAGS|PKG_LIBS);

		pkgconf_client_set_flags(&pkg_client, want_client_flags | PKGCONF_PKG_PKGF_SKIP_ERRORS);
		if (!pkgconf_queue_apply(&pkg_client, &pkgq, apply_simulate, -1, NULL))
		{
			ret = EXIT_FAILURE;
			goto out;
		}
	}

	if (!pkgconf_queue_validate(&pkg_client, &pkgq, maximum_traverse_depth))
	{
		ret = EXIT_FAILURE;
		goto out;
	}

	if ((want_flags & PKG_VALIDATE) == PKG_VALIDATE)
		return 0;

	if ((want_flags & PKG_UNINSTALLED) == PKG_UNINSTALLED)
	{
		ret = EXIT_FAILURE;
		pkgconf_queue_apply(&pkg_client, &pkgq, apply_uninstalled, maximum_traverse_depth, &ret);
		goto out;
	}

	if (want_env_prefix != NULL)
	{
		if (!pkgconf_queue_apply(&pkg_client, &pkgq, apply_env, maximum_traverse_depth, want_env_prefix))
		{
			ret = EXIT_FAILURE;
			goto out;
		}

		want_flags = 0;
	}

	if ((want_flags & PKG_PROVIDES) == PKG_PROVIDES)
	{
		want_flags &= ~(PKG_CFLAGS|PKG_LIBS);

		if (!pkgconf_queue_apply(&pkg_client, &pkgq, apply_provides, maximum_traverse_depth, NULL))
		{
			ret = EXIT_FAILURE;
			goto out;
		}
	}

	if ((want_flags & PKG_DIGRAPH) == PKG_DIGRAPH)
	{
		want_flags &= ~(PKG_CFLAGS|PKG_LIBS);

		if (!pkgconf_queue_apply(&pkg_client, &pkgq, apply_digraph, maximum_traverse_depth, NULL))
		{
			ret = EXIT_FAILURE;
			goto out;
		}
	}

	if ((want_flags & PKG_MODVERSION) == PKG_MODVERSION)
	{
		want_flags &= ~(PKG_CFLAGS|PKG_LIBS);

		if (!pkgconf_queue_apply(&pkg_client, &pkgq, apply_modversion, maximum_traverse_depth, NULL))
		{
			ret = EXIT_FAILURE;
			goto out;
		}
	}

	if ((want_flags & PKG_PATH) == PKG_PATH)
	{
		want_flags &= ~(PKG_CFLAGS|PKG_LIBS);

		pkgconf_client_set_flags(&pkg_client, want_client_flags | PKGCONF_PKG_PKGF_SKIP_ROOT_VIRTUAL);
		if (!pkgconf_queue_apply(&pkg_client, &pkgq, apply_path, maximum_traverse_depth, NULL))
		{
			ret = EXIT_FAILURE;
			goto out;
		}
	}

	if ((want_flags & PKG_VARIABLES) == PKG_VARIABLES)
	{
		want_flags &= ~(PKG_CFLAGS|PKG_LIBS);

		if (!pkgconf_queue_apply(&pkg_client, &pkgq, apply_variables, maximum_traverse_depth, NULL))
		{
			ret = EXIT_FAILURE;
			goto out;
		}
	}

	if (want_variable)
	{
		want_flags &= ~(PKG_CFLAGS|PKG_LIBS);

		pkgconf_client_set_flags(&pkg_client, want_client_flags | PKGCONF_PKG_PKGF_SKIP_ROOT_VIRTUAL);
		if (!pkgconf_queue_apply(&pkg_client, &pkgq, apply_variable, maximum_traverse_depth, want_variable))
		{
			ret = EXIT_FAILURE;
			goto out;
		}
	}

	if ((want_flags & PKG_REQUIRES) == PKG_REQUIRES)
	{
		want_flags &= ~(PKG_CFLAGS|PKG_LIBS);

		if (!pkgconf_queue_apply(&pkg_client, &pkgq, apply_requires, maximum_traverse_depth, NULL))
		{
			ret = EXIT_FAILURE;
			goto out;
		}
	}

	if ((want_flags & PKG_REQUIRES_PRIVATE) == PKG_REQUIRES_PRIVATE)
	{
		want_flags &= ~(PKG_CFLAGS|PKG_LIBS);

		pkgconf_client_set_flags(&pkg_client, want_client_flags | PKGCONF_PKG_PKGF_SEARCH_PRIVATE);

		if (!pkgconf_queue_apply(&pkg_client, &pkgq, apply_requires_private, maximum_traverse_depth, NULL))
		{
			ret = EXIT_FAILURE;
			goto out;
		}

		pkgconf_client_set_flags(&pkg_client, want_client_flags);
	}

	if ((want_flags & PKG_CFLAGS))
	{
		pkgconf_client_set_flags(&pkg_client, want_client_flags | PKGCONF_PKG_PKGF_SEARCH_PRIVATE);

		if (!pkgconf_queue_apply(&pkg_client, &pkgq, apply_cflags, maximum_traverse_depth, NULL))
		{
			ret = EXIT_FAILURE;
			goto out_println;
		}

		pkgconf_client_set_flags(&pkg_client, want_client_flags);
	}

	if ((want_flags & PKG_LIBS))
	{
		if (!pkgconf_queue_apply(&pkg_client, &pkgq, apply_libs, maximum_traverse_depth, NULL))
		{
			ret = EXIT_FAILURE;
			goto out_println;
		}
	}

	pkgconf_queue_free(&pkgq);

out_println:
	if (want_flags & (PKG_CFLAGS|PKG_LIBS))
		printf("\n");

out:
	pkgconf_client_deinit(&pkg_client);

	if (logfile_out != NULL)
		fclose(logfile_out);

	return ret;
}