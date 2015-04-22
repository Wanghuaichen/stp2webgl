/* $RCSfile: $
 * $Revision: $ $Date: $
 * Auth: David Loffredo (loffredo@steptools.com)
 * 
 * Copyright (c) 1991-2015 by STEP Tools Inc. 
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify, and distribute this software and
 * its documentation is hereby granted, provided that this copyright
 * notice and license appear on all copies of the software.
 * 
 * STEP TOOLS MAKES NO REPRESENTATIONS OR WARRANTIES ABOUT THE
 * SUITABILITY OF THE SOFTWARE, EITHER EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT. STEP TOOLS
 * SHALL NOT BE LIABLE FOR ANY DAMAGES SUFFERED BY LICENSEE AS A
 * RESULT OF USING, MODIFYING OR DISTRIBUTING THIS SOFTWARE OR ITS
 * DERIVATIVES.
 */

#include <stp_schema.h>
#include <stix.h>
#include <stixmesh.h>

#include "stp2webgl.h"


// write_binary_stl() -- write a single STL file for a STEP model.
// This facets everything in one pass, and then work on the cached
// data.  It then recursively walks down through any assemblies,
// applying transforms to the facet data and writing Binary STL.
//

extern void facet_all_products (stp2webgl_opts * opts);
extern int write_binary_stl (stp2webgl_opts * opts);

static void write_float (FILE * file, double val);
static void write_unsigned (FILE * file, unsigned val);

static unsigned count_mesh_for_product (
    stp_product_definition * pd
    ) ;
static void print_mesh_for_product (
    FILE * stlfile,
    stp_product_definition * pd,
    StixMtrx &starting_placement
    );

// ======================================================================


extern int write_binary_stl (stp2webgl_opts * opts)
{    
    FILE * stlfile = stdout;
    unsigned i,sz;
    unsigned count = 0;
    
    if (opts->do_split)
    {
	printf ("Only single STL file output currently implemented\n");
	return 2;
    }
    
    if (opts->dstfile)
    {
	stlfile = rose_fopen(opts->dstfile, "wb");
	if (!stlfile) {
	    printf ("Could not open output file\n");
	    return 2;
	}
    }

    // Recursively facet all of the products in the root assemblies
    // and attach each resulting mesh to the representation item for
    // each solid.
    //
    facet_all_products(opts);

    // Now print the mesh details along with placement info
    for (i=0, sz=opts->root_prods.size(); i<sz; i++)
    {
	count += count_mesh_for_product (opts->root_prods[i]);
    }

    unsigned char buf[80];

    memset (buf, 0, 80);
    strcpy ((char*)buf, "binary stl");
    fwrite (buf, sizeof (unsigned char), 80, stlfile);
    write_unsigned(stlfile, count);
    
    // Now print the mesh details along with placement info
    for (i=0, sz=opts->root_prods.size(); i<sz; i++)
    {
	// The root placement is usually the identity matrix but some
	// systems put a standalone AP3D at the top to place the whole
	// thing in the global space.
	StixMtrx root_placement; 

	print_mesh_for_product (stlfile, opts->root_prods[i], root_placement);
    }

    fclose(stlfile);
    return 0;
}



//------------------------------------------------------------
//------------------------------------------------------------
// COUNT FACETS -- Binary STL needs an upfront count, which we
// need to compute ahead of time.
//

unsigned count_mesh_for_shape (
    stp_representation * rep
    )
{
    unsigned i, sz;
    unsigned count = 0;

    if (!rep) return count;
    
    // Count any local meshes
    SetOfstp_representation_item * items = rep->items();
    for (i=0, sz=items->size(); i<sz; i++) 
    {
	stp_representation_item  * it = items->get(i);
	StixMeshStp * mesh = stixmesh_cache_find (it);
	if (!mesh) continue;

	count +=  mesh-> getFacetSet()->getFacetCount();
    }

    // Count all of the child shapes 
    StixMgrAsmShapeRep * rep_mgr = StixMgrAsmShapeRep::find(rep);
    if (!rep_mgr) return count;

    for (i=0, sz=rep_mgr->child_rels.size(); i<sz; i++) 
    {
	stp_shape_representation_relationship * rel = rep_mgr->child_rels[i];
	stp_representation * child = stix_get_shape_usage_child_rep (rel);
	count += count_mesh_for_shape (child);
    }

    for (i=0, sz=rep_mgr->child_mapped_items.size(); i<sz; i++) 
    {
	stp_mapped_item * rel = rep_mgr->child_mapped_items[i];
	stp_representation * child = stix_get_shape_usage_child_rep (rel);
	count += count_mesh_for_shape (child);
    }
    return count;
}


unsigned count_mesh_for_product (
    stp_product_definition * pd
    ) 
{
    unsigned i, sz;
    unsigned count = 0;
    StixMgrAsmProduct * pm = StixMgrAsmProduct::find(pd);
    if (!pm) return count;

    for (i=0, sz=pm->shapes.size(); i<sz; i++) 
    {
	stp_shape_representation * rep = pm->shapes[i];
	count += count_mesh_for_shape (rep);
    }
    return count;
}




//------------------------------------------------------------
//------------------------------------------------------------
// PRINT THE FACET INFORMATION -- This follows the shape information
// attached to a single product or assembly and prints it to the STL
// file.  This is adapted from the stixmesh facet assembly sample.
//
// Since the shapes are in a tree that parallels the product tree, we
// look for attached next_assembly_usage_occurrences (NAUO) that tell
// us when we are moving into the shape of another product.
//------------------------------------------------------------
//------------------------------------------------------------

static void print_triangle (
    FILE * stlfile,
    const StixMeshFacetSet * fs,
    StixMtrx &xform,
    unsigned facet_num
    )
{
    double v[3];
    double n[3];
    const StixMeshFacet * f = fs-> getFacet(facet_num);
    if (!f) return;

    // The components of the triangle verticies and vertex normals are
    // given by an index into internal tables.  Apply the transform so
    // that the facet is placed correctly in the part space.
    //
#ifdef FACET_NORMAL_NOW_COMPUTED_IN_LATEST_VERSIONS
    fs->getFacetNormal(n, f);
    stixmesh_transform_dir (n, xform, n); 
#else
    stixmesh_transform_dir (n, xform, fs-> getNormal(f-> facet_normal));
#endif
    write_float(stlfile, n[0]);
    write_float(stlfile, n[1]);
    write_float(stlfile, n[2]);

    stixmesh_transform (v, xform, fs-> getVertex(f-> verts[0]));
    write_float(stlfile, v[0]);
    write_float(stlfile, v[1]);
    write_float(stlfile, v[2]);

    stixmesh_transform (v, xform, fs-> getVertex(f-> verts[1]));
    write_float(stlfile, v[0]);
    write_float(stlfile, v[1]);
    write_float(stlfile, v[2]);

    stixmesh_transform (v, xform, fs-> getVertex(f-> verts[2]));
    write_float(stlfile, v[0]);
    write_float(stlfile, v[1]);
    write_float(stlfile, v[2]);

    putc(0, stlfile);	// 16bit zero
    putc(0, stlfile);
}


static void print_mesh_for_shape (
    FILE * stlfile,
    stp_representation * rep,
    StixMtrx &rep_xform
    )
{
    unsigned i, sz;
    unsigned j, szz;

    if (!rep) return;
    
    // Does the rep have any meshed items?  In an assembly, some reps
    // just contain placements for transforming components. If there
    // are solids, we should have previously generated meshes.
    //
    SetOfstp_representation_item * items = rep->items();
    for (i=0, sz=items->size(); i<sz; i++) 
    {
	stp_representation_item  * it = items->get(i);
	StixMeshStp * mesh = stixmesh_cache_find (it);
	if (!mesh) continue;

	const StixMeshFacetSet * fs = mesh-> getFacetSet();

	for (j=0, szz=fs->getFacetCount(); j< szz; j++) {
	    print_triangle (stlfile, fs, rep_xform, j);
	}
    }


    // Go through all of the child shapes which can be attached by a
    // shape_reprepresentation_relationship or a mapped_item.  If the
    // relation has a NAUO associated with it, then it is the start of
    // a different product, otherwise it is still part of the shape of
    // this one.
    //
    StixMgrAsmShapeRep * rep_mgr = StixMgrAsmShapeRep::find(rep);
    if (!rep_mgr) return;

    for (i=0, sz=rep_mgr->child_rels.size(); i<sz; i++) 
    {
	stp_shape_representation_relationship * rel = rep_mgr->child_rels[i];
	stp_representation * child = stix_get_shape_usage_child_rep (rel);

	// Move to location in enclosing asm
	StixMtrx child_xform = stix_get_shape_usage_xform (rel);
	child_xform = child_xform * rep_xform;

	print_mesh_for_shape (stlfile, child, child_xform);
    }


    for (i=0, sz=rep_mgr->child_mapped_items.size(); i<sz; i++) 
    {
	stp_mapped_item * rel = rep_mgr->child_mapped_items[i];
	stp_representation * child = stix_get_shape_usage_child_rep (rel);

	// Move to location in enclosing asm
	StixMtrx child_xform = stix_get_shape_usage_xform (rel);
	child_xform = child_xform * rep_xform;

	print_mesh_for_shape (stlfile, child, child_xform);
    }
}


static void print_mesh_for_product (
    FILE * stlfile,
    stp_product_definition * pd,
    StixMtrx &starting_placement
    ) 
{
    // Print the shape tree for each shape associated with a product,
    // and then follow the shape tree downward.  At each level we
    // check the shape relationship for a link to product relations
    // because shape side because there can be relationships there
    // that are not linked to products.
    //
    unsigned i, sz;
    StixMgrAsmProduct * pm = StixMgrAsmProduct::find(pd);
    if (!pm) return;

    for (i=0, sz=pm->shapes.size(); i<sz; i++) 
    {
	stp_shape_representation * rep = pm->shapes[i];
	print_mesh_for_shape (stlfile, rep, starting_placement);
    }
}



//------------------------------------------------------------
//------------------------------------------------------------
// Binary utilities -- Binary STL uses little endian 32bit float, and
// little endan 32bit unsigned integer. This matches the common intel
// usage for windows, mac and linux.  We swap big endian if working on
// aix, sparc, hpux or ppc macs.
//------------------------------------------------------------
//------------------------------------------------------------

#if defined(_AIX) || defined(__sparc) || defined(__hpux)
#define BIG_ENDIAN
#endif

#ifdef __APPLE__
#if defined (__ppc__) || defined(__ppc64__)
#define BIG_ENDIAN
#endif
#endif

static void write_float (FILE * file, double val)
{    
    union {
	float 		float_elem;	/* assume 32bit float */
	unsigned char  	words[4];
    } olbuf;

    olbuf.float_elem = (float) val;

#ifdef BIG_ENDIAN
    putc (olbuf.words[3], file);
    putc (olbuf.words[2], file);
    putc (olbuf.words[1], file);
    putc (olbuf.words[0], file);
#else
    putc (olbuf.words[0], file);
    putc (olbuf.words[1], file);
    putc (olbuf.words[2], file);
    putc (olbuf.words[3], file);
#endif
}

static void write_unsigned (FILE * file, unsigned val)
{    
    // shifts work properly regardless of endian-ness
    putc (val & 0xff, file);
    putc ((val >> 8) & 0xff, file);
    putc ((val >> 16) & 0xff, file);
    putc ((val >> 24) & 0xff, file);
}