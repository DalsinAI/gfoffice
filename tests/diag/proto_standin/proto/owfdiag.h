/* tests/diag/check_proto_order.sh's stand-in for owfdiag.library's <proto/owfdiag.h> on the PC: a function that
 * needs every type the library's calls take to be complete, as the real inline calls do. */
static inline unsigned long odg_standin_complete(odg_doc *d, odg_rexx *r, odg_clip *c, owf_diagram *g,
                                                 const odg_stencil *s, odg_order_id *o, odg_mind_key k, odg_prop p,
                                                 odg_align_how a)
{
    return sizeof *d + sizeof *r + sizeof *c + sizeof *g + sizeof *s + sizeof *o + (unsigned long)k +
           (unsigned long)p + (unsigned long)a;
}
