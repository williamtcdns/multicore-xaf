/*
* Copyright (c) 2015-2024 Cadence Design Systems Inc.
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be included
* in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
/*******************************************************************************
 * rbtree.h
 *
 * Generic implmentation of red-black trees
 *******************************************************************************/

#ifndef __XF_RBTREE_H
#define __XF_RBTREE_H

/*******************************************************************************
 * Red-black tree node definition
 ******************************************************************************/

/* ...reference to rb-tree node */
typedef struct xf_rb_node  *xf_rb_idx_t;

/* ...rb-tree node */
typedef struct xf_rb_node
{
    /* ...pointers to parent and two children */
    xf_rb_idx_t            parent, left, right;

    /* ...node color (least-significant-bit only) */
    UWORD64                 color;

}   xf_rb_node_t;

/* ...rb-tree data */
typedef struct xf_rb_tree_t
{
    /* ...tree sentinel node */
    xf_rb_node_t           root;

}   xf_rb_tree_t;

/*******************************************************************************
 * Helpers
 ******************************************************************************/

/* ...left child accessor */
static inline xf_rb_idx_t xf_rb_left(xf_rb_tree_t *tree, xf_rb_idx_t n_idx)
{
    return n_idx->left;
}

/* ...right child accessor */
static inline xf_rb_idx_t xf_rb_right(xf_rb_tree_t *tree, xf_rb_idx_t n_idx)
{
    return n_idx->right;
}

/* ...parent node accessor */
static inline xf_rb_idx_t xf_rb_parent(xf_rb_tree_t *tree, xf_rb_idx_t n_idx)
{
    return n_idx->parent;
}

/* ...tree root node accessor */
static inline xf_rb_idx_t xf_rb_root(xf_rb_tree_t *tree)
{
    return xf_rb_left(tree, &tree->root);
}

/* ...tree data pointer accessor */
static inline xf_rb_idx_t xf_rb_cache(xf_rb_tree_t *tree)
{
    return xf_rb_right(tree, &tree->root);
}

/* ...tree null node */
static inline xf_rb_idx_t xf_rb_null(xf_rb_tree_t *tree)
{
    return &tree->root;
}

/* ...get user-bits stored in node color */
static inline UWORD64 xf_rb_node_data(xf_rb_tree_t *tree, xf_rb_idx_t n_idx)
{
    return (n_idx->color >> 1);
}

/* ...left child assignment */
static inline void xf_rb_set_left(xf_rb_tree_t *tree, xf_rb_idx_t n_idx, xf_rb_node_t *child)
{
    n_idx->left = child;
}

/* ...right child assignment */
static inline void xf_rb_set_right(xf_rb_tree_t *tree, xf_rb_idx_t n_idx, xf_rb_node_t *child)
{
    n_idx->right = child;
}

/* ...cache tree client index */
static inline void xf_rb_set_cache(xf_rb_tree_t *tree, xf_rb_idx_t c_idx)
{
    tree->root.right = c_idx;
}

/* ...get user-bits stored in node color */
static inline void xf_rb_set_node_data(xf_rb_tree_t *tree, xf_rb_idx_t n_idx, UWORD32 data)
{
    n_idx->color = (n_idx->color & 0x1) | (data << 1);
}

/*******************************************************************************
 * API functions
 ******************************************************************************/

/* ...initialize rb-tree */
extern void     xf_rb_init(xf_rb_tree_t *tree);

/* ...insert node into tree as a child of p */
extern void     xf_rb_insert(xf_rb_tree_t *tree, xf_rb_idx_t n_idx, xf_rb_idx_t p_idx);

/* ...replace the node with same-key value and fixup tree pointers */
extern void     xf_rb_replace(xf_rb_tree_t *tree, xf_rb_idx_t n_idx, xf_rb_idx_t t_idx);

/* ...delete node from the tree and return its in-order predecessor/successor */
extern xf_rb_idx_t xf_rb_delete(xf_rb_tree_t *tree, xf_rb_idx_t n_idx);

/* ...first in-order item in the tree */
extern xf_rb_idx_t xf_rb_first(xf_rb_tree_t *tree);

/* ...last in-order item in the tree */
extern xf_rb_idx_t xf_rb_last(xf_rb_tree_t *tree);

/* ...forward tree iterator */
extern xf_rb_idx_t xf_rb_next(xf_rb_tree_t *tree, xf_rb_idx_t n_idx);

/* ...backward tree iterator */
extern xf_rb_idx_t xf_rb_prev(xf_rb_tree_t *tree, xf_rb_idx_t n_idx);

#endif  /* __XF_RBTREE_H */
