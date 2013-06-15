/* del.c - Alpine Package Keeper (APK)
 *
 * Copyright (C) 2005-2008 Natanael Copa <n@tanael.org>
 * Copyright (C) 2008-2011 Timo Teräs <timo.teras@iki.fi>
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation. See http://www.gnu.org/ for details.
 */

#include <stdio.h>
#include "apk_applet.h"
#include "apk_database.h"
#include "apk_print.h"
#include "apk_solver.h"

struct del_ctx {
	int recursive_delete : 1;
	struct apk_dependency_array *world;
};

static int del_parse(void *pctx, struct apk_db_options *db,
		     int optch, int optindex, const char *optarg)
{
	struct del_ctx *ctx = (struct del_ctx *) pctx;

	switch (optch) {
	case 'r':
		ctx->recursive_delete = 1;
		break;
	default:
		return -1;
	}
	return 0;
}

struct not_deleted_ctx {
	struct apk_indent indent;
	struct apk_name *name;
	unsigned int matches;
	int header;
};

static void print_not_deleted_message(
	struct apk_package *pkg0, struct apk_dependency *dep0,
	struct apk_package *pkg, void *pctx)
{
	struct not_deleted_ctx *ctx = (struct not_deleted_ctx *) pctx;

	if (!ctx->header) {
		apk_message("World updated, but the following packages are not removed due to:");
		ctx->header = 1;
	}
	if (!ctx->indent.indent) {
		ctx->indent.x = printf("  %s:", ctx->name->name);
		ctx->indent.indent = ctx->indent.x + 1;
	}

	apk_print_indented(&ctx->indent, APK_BLOB_STR(pkg0->name->name));
	apk_pkg_foreach_reverse_dependency(pkg0, ctx->matches, print_not_deleted_message, pctx);
}

static void delete_from_world(
	struct apk_package *pkg0, struct apk_dependency *dep0,
	struct apk_package *pkg, void *pctx)
{
	struct del_ctx *ctx = (struct del_ctx *) pctx;

	apk_deps_del(&ctx->world, pkg0->name);

	if (ctx->recursive_delete)
		apk_pkg_foreach_reverse_dependency(
			pkg0, APK_FOREACH_INSTALLED | APK_DEP_SATISFIES,
			delete_from_world, pctx);
}

static int del_main(void *pctx, struct apk_database *db, int argc, char **argv)
{
	struct del_ctx *ctx = (struct del_ctx *) pctx;
	struct not_deleted_ctx ndctx = {};
	struct apk_name **name;
	struct apk_changeset changeset = {};
	struct apk_change *change;
	struct apk_provider *p;
	int i, r = 0;

	apk_dependency_array_copy(&ctx->world, db->world);

	name = alloca(argc * sizeof(struct apk_name*));
	for (i = 0; i < argc; i++) {
		name[i] = apk_db_get_name(db, APK_BLOB_STR(argv[i]));
		delete_from_world(apk_pkg_get_installed(name[i]), NULL, NULL, ctx);
	}

	r = apk_solver_solve(db, 0, ctx->world, &changeset);
	if (r == 0) {
		/* check for non-deleted package names */
		foreach_array_item(change, changeset.changes) {
			struct apk_package *pkg = change->new_pkg;
			if (pkg == NULL)
				continue;
			pkg->marked = 1;
		}
		for (i = 0; i < argc; i++) {
			ndctx.indent.indent = 0;
			ndctx.name = name[i];
			ndctx.matches = apk_foreach_genid() | APK_FOREACH_MARKED | APK_DEP_SATISFIES;
			foreach_array_item(p, name[i]->providers) {
				if (!p->pkg->marked)
					continue;
				print_not_deleted_message(p->pkg, NULL, NULL, &ndctx);
			}
			if (ndctx.indent.indent)
				printf("\n");
		}
		if (ndctx.header)
			printf("\n");
		apk_solver_commit_changeset(db, &changeset, ctx->world);
		r = 0;
	} else {
		apk_solver_print_errors(db, &changeset, ctx->world);
	}
	apk_dependency_array_free(&ctx->world);

	return r;
}

static struct apk_option del_options[] = {
	{ 'r', "rdepends",	"Recursively delete all top-level reverse "
				"dependencies too." },
};

static struct apk_applet apk_del = {
	.name = "del",
	.help = "Remove PACKAGEs from the main dependencies and uninstall them.",
	.arguments = "PACKAGE...",
	.open_flags = APK_OPENF_WRITE,
	.context_size = sizeof(struct del_ctx),
	.num_options = ARRAY_SIZE(del_options),
	.options = del_options,
	.parse = del_parse,
	.main = del_main,
};

APK_DEFINE_APPLET(apk_del);

