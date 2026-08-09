/* Standalone failure sweep for a document-owned template fragment. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "liblexa/html.h"

typedef struct {
    lxa_allocator_t base;
    size_t calls, fail_at, live;
} state_t;

static void *
allocate(void *context, size_t size)
{
    state_t *state = context;
    void *ptr;
    if (state->calls++ == state->fail_at) return NULL;
    ptr = state->base.allocate(state->base.context, size);
    if (ptr != NULL) state->live++;
    return ptr;
}

static void *
resize(void *context, void *ptr, size_t size)
{
    state_t *state = context;
    void *next;
    if (state->calls++ == state->fail_at) return NULL;
    next = state->base.resize(state->base.context, ptr, size);
    if (next != NULL && ptr == NULL) state->live++;
    return next;
}

static void
release(void *context, void *ptr)
{
    state_t *state = context;
    if (ptr != NULL) {
        if (state->live == 0) abort();
        state->live--;
    }
    state->base.release(state->base.context, ptr);
}

static int
sweep(const unsigned char *input, size_t length, const char *name)
{
    state_t state = {0};
    lxa_allocator_t allocator;
    size_t failure;
    int success = 0;
    state.base = lxa_allocator_default();
    allocator = (lxa_allocator_t){&state, allocate, resize, release};
    for (failure = 0; failure < 256; failure++) {
        lxa_html_document_t *document = NULL;
        lxa_html_result_t result;
        lxa_limit_reason_t reason;
        lxa_status_t status;
        state.calls = 0;
        state.fail_at = failure;
        status = lxa_html_document_parse(&allocator, NULL,
            (lxa_span_t){input, length},
            &result, &reason, &document);
        state.fail_at = SIZE_MAX; /* Validation is outside the parse sweep. */
        if (status == LXA_OK) {
            if (result != LXA_HTML_OK || document == NULL
                || lxa_dom_nodes_validate(lxa_html_document_nodes(document))
                   != LXA_OK)
                return 1;
            success = 1;
        }
        else if (status != LXA_ERROR_NO_MEMORY || document != NULL)
            return 2;
        lxa_html_document_destroy(document);
        if (state.live != 0) return 3;
        if (success) break;
    }
    if (!success) return 4;
    printf("%s: %zu allocation failure points, no live blocks\n",
           name, failure);
    return 0;
}

int
main(void)
{
    static const unsigned char unclosed[] =
        "<div><template><div><frameset><span><sub><frame>"
        "</template><b>";
    static const unsigned char after_head[] =
        "<head></head><template><object><div>Foo</template><head>";
    static const unsigned char nested_section_select[] =
        "<head><template><template><tbody><select>"
        "</template></template>";
    static const unsigned char foreign_template[] =
        "<body><svg><template><g></g></template></svg>";
    static const unsigned char nested_foreign_template[] =
        "<head><template><svg><template></template></svg>"
        "<p>x</p></template>";
    static const unsigned char math_template[] =
        "<template><math><mi>x</mi><mn>2</mn></math></template>";
    static const unsigned char integrated_math_template[] =
        "<template><math><mi><b>x</b></mi><mtext>"
        "<em>y</em></mtext></math></template>";
    static const unsigned char math_block_template[] =
        "<template><math><mtext><div id=d>x</div></mtext>"
        "<mn><span>y</span></mn></math></template>";
    static const unsigned char math_void_template[] =
        "<template><math><mi><img src=x></mi><mtext>"
        "<br><hr></mtext></math></template>";
    static const unsigned char math_select_template[] =
        "<template><math><mtext><select><optgroup label=a>"
        "<option>x</option></optgroup></select></mtext></math></template>";
    static const unsigned char math_select_input[] =
        "<template><math><mtext><select><option>a"
        "<input type=text>b</select></mtext></math></template>";
    static const unsigned char math_text_modes[] =
        "<template><math><mtext><textarea>a&amp;b</textarea>"
        "<title>t&amp;u</title><style>b{c:d}</style>"
        "</mtext></math></template>";
    static const unsigned char math_ruby[] =
        "<template><math><mtext><ruby>x<rt>a</rt></ruby>"
        "</mtext><annotation-xml encoding=text/html>"
        "<ruby>y<rp>(<rt>b<rp>)</ruby></annotation-xml>"
        "</math></template>";
    static const unsigned char math_block_breakout[] =
        "<template><div><math><mrow><p>x</p></mrow>"
        "</math>y</div><math><mrow><mrow><div>z</div>"
        "</mrow></mrow></math><math><mrow><ruby>r</ruby>"
        "</mrow></math></template>";
    static const unsigned char math_inline_list_breakout[] =
        "<template><math><mrow><span>x</span></mrow></math>"
        "<math><mrow><b>y</b></mrow></math>"
        "<math><mrow><ul><li>z</li></ul></mrow></math>"
        "</template>";
    static const unsigned char math_foreign_blocks[] =
        "<template><math><mrow><section>x</section>"
        "<article>y</article><p>z</p></mrow></math></template>";
    static const unsigned char math_annotation_template[] =
        "<template><math><annotation-xml encoding=application/xml>"
        "<mi>a</mi></annotation-xml><annotation-xml "
        "encoding='TeXt/HtMl'><div>x</div></annotation-xml>"
        "</math></template>";
    static const unsigned char math_annotation_svg[] =
        "<template><math><annotation-xml encoding=application/xml>"
        "<svg><g>x</g></svg></annotation-xml></math></template>";
    static const unsigned char math_descendant_svg[] =
        "<template><math><mtext><svg><g>x</g></svg></mtext>"
        "<mrow><svg><g>y</g></svg></mrow></math></template>";
    static const unsigned char svg_breakout_template[] =
        "<template><svg><g><p>x</p></g></svg>"
        "<svg><g><g><div>y</div></g></g></svg></template>";
    static const unsigned char svg_foreign_section[] =
        "<template><svg><g><section>x</section>"
        "<p>y</p></g></svg></template>";
    static const unsigned char svg_inline_breakout[] =
        "<template><svg><g><b>x</b></g></svg>"
        "<svg><g><span>y</span></g></svg></template>";
    static const unsigned char svg_sub_sup_breakout[] =
        "<template><svg><g><sub>x</sub></g></svg>"
        "<svg><g><sup>y</sup></g></svg></template>";
    static const unsigned char svg_more_inline_breakout[] =
        "<template><svg><g><big>a</big></g></svg>"
        "<svg><g><small>b</small></g></svg>"
        "<svg><g><s>c</s></g></svg>"
        "<svg><g><strike>d</strike></g></svg>"
        "<svg><g><tt>e</tt></g></svg>"
        "<svg><g><u>f</u></g></svg>"
        "<svg><g><var>g</var></g></svg>"
        "<svg><g><nobr>h</nobr></g></svg></template>";
    static const unsigned char svg_embed_meta_breakout[] =
        "<template><svg><g><embed src=x></g></svg>"
        "<svg><g><meta name=x></g></svg></template>";
    static const unsigned char svg_menu_listing_breakout[] =
        "<template><svg><g><menu><li>x</li></menu></g></svg>"
        "<svg><g><listing>\ny</listing></g></svg></template>";
    static const unsigned char svg_font_breakout[] =
        "<template><svg><g><font color=red>x</font></g></svg>"
        "<svg><g><font face=serif>y</font></g></svg>"
        "<svg><g><font>z</font></g></svg></template>";
    static const unsigned char svg_body_breakout[] =
        "<template><svg><g><body id=x>text</body></g></svg>"
        "<svg><body>more</body></svg></template>";
    static const unsigned char svg_ruby_breakout[] =
        "<template><svg><g><ruby><span>x</span></ruby>"
        "</g></svg></template>";
    static const unsigned char ruby_annotations[] =
        "<template><ruby>base<rp>(<rt>a<rp>)</ruby>"
        "<svg><g><ruby>base<rt>a<rt>b</ruby></g></svg>"
        "</template>";
    static const unsigned char nested_ruby_annotations[] =
        "<template><ruby><span>x<rt>a<rt>b</ruby>"
        "<ruby><rt>x<span>y<rt>z</ruby></template>";
    static const unsigned char nested_template_foreign[] =
        "<template><div><svg><g>x</g></svg><math><mi>y</mi>"
        "</math></div></template>";
    static const unsigned char svg_integrated_html[] =
        "<template><svg><title><b>x</b></title><desc>"
        "<span>y</span></desc></svg></template>";
    static const unsigned char foreign_object_html[] =
        "<template><svg><foreignObject><p>x</p>"
        "<span>y</span></foreignObject></svg></template>";
    static const unsigned char svg_void_breakout[] =
        "<template><svg><g><br><img src=/x><hr></g>"
        "</svg></template>";
    static const unsigned char svg_list_breakout[] =
        "<template><svg><g><ul><li>x</li></ul></g></svg>"
        "<svg><g><ol><li>y</li></ol></g></svg></template>";
    static const unsigned char svg_block_breakout[] =
        "<template><svg><g><h2>x</h2></g></svg>"
        "<svg><g><table><td>y</table></g></svg></template>";
    static const unsigned char svg_definition_breakout[] =
        "<template><svg><g><blockquote>x</blockquote></g></svg>"
        "<svg><g><center>y</center></g></svg>"
        "<svg><g><dl><dt>a</dt><dd>b</dd></dl></g></svg></template>";
    static const unsigned char integrated_foreign_template[] =
        "<template><svg><foo><template><foreignObject><div>"
        "</template><div>";
    static const unsigned char crossed_anchor_paragraph[] =
        "<a>1<p>2</a>3</p>";
    static const unsigned char crossed_anchor_formatting[] =
        "<a>1<b>2</a>3</b>";
    static const unsigned char fostered_template_anchor[] =
        "<template><a><table><a>x</a></table></template>";
    static const unsigned char outer_frameset[] =
        "<frameset><template><frame src=x></frame></template></frameset>";
    static const unsigned char nested_frameset[] =
        "<frameset><frameset id=x><frame></frameset>"
        "<frame></frameset>";
    static const unsigned char frameset_noframes[] =
        "<frameset><noframes>fallback <b>text</noframes>"
        "<frame></frameset><noframes>later";
    static const unsigned char nested_template_object[] =
        "<template><p><object>v</object>z</p></template>";
    static const unsigned char template_object_select[] =
        "<template><p><object><select><option>x</select>"
        "y</object>z</p></template>";
    static const unsigned char formatted_template_object[] =
        "<template><p><b class=x><object id=o>v</object>z</b>"
        "</p></template>";
    static const unsigned char block_template_object[] =
        "<template><ul><li><object id=o>x</object>y</li></ul>"
        "<section><object>z</object></section></template>";
    static const unsigned char table_cell_object[] =
        "<template><table><tr><td><object id=o>x</object>y"
        "</td></tr></table></template>";
    static const unsigned char fostered_template_object[] =
        "<template><table><object>x</object><tr><td>z</td></tr>"
        "</table></template>";
    static const unsigned char table[] =
        "<body><template><table><tr><td>x</td></tr></table></template>";
    static const unsigned char virtual_foreign_recovery[] =
        "<template><tr><caption><div><col>x<td>y</template>"
        "<template><td><tr><div><col>x<td>y</template>"
        "<template><thead><th><form>x<colgroup><col>y</template>"
        "<form><template><tr><td><form>x<tfoot>y</template></form><form>z</form>"
        "<template><tbody><math><unknown><mtext><tr><td>y</template>"
        "<template><caption>x<caption>y</template>"
        "<template><caption>x<colgroup>y</template>"
        "<template><caption>x<tbody>y</template>"
        "<template><caption>x<tr>y</template>"
        "<template><caption>x<td>y</template>"
        "<template><td><tr><div><tfoot>y</div></template>"
        "<template><tr><caption><div><tbody>y</div></template>"
        "<template><td><tr><div><td>y</td>z</template>"
        "<template><tr><caption><div><td>y</td>z</template>";
    static const unsigned char option_pop_selectedcontent[] =
        "<template><select><selectedcontent><selectedcontent>old"
        "</selectedcontent></selectedcontent>"
        "<option selected><b href=x>x</b></option><option selected>"
        "<template><selectedcontent>inert</selectedcontent></template>"
        "<i>y</i>";
    static const unsigned char disabled_selectedcontent[] =
        "<option><selectedcontent href=x>own</selectedcontent></option>"
        "<selectedcontent><selectedcontent>nested</selectedcontent></selectedcontent>";
    static const unsigned char option_select_reset[] =
        "<select><option>x<option selected>y</select>"
        "<template><select><option>x<option selected>y</select></template>";
    static const unsigned char section_list[] =
        "<head><template><ul><li>x<section><li>later</template>";
    static const unsigned char formatted_section_list[] =
        "<head><template><ul><li>x<section><b>t<li>later</template>";
    static const unsigned char hidden_table_input[] =
        "<head><template><table><col><input type=hidden>"
        "<tr><input type=HIDDEN><td>x</template>";
    static const unsigned char fostered_table_input[] =
        "<head><template><table><form id=a><col><form id=b>"
        "<p>block</p><input type=text>"
        "<ul><li>list</ul><li>first<li>second</li>"
        "<image src=/a><span>text</span><br>"
        "<select><option>choice</select>"
        "<tr><input name=x>"
        "<img src=/b><td>x</template>";
    static const unsigned char fostered_definition[] =
        "<template><table><tbody><blockquote>x</blockquote>"
        "<tr><td>y</td></tr></tbody></table>"
        "<table><dl><dt>a</dt><dd>b</dd></dl>"
        "<tr><td>z</td></tr></table></template>";
    static const unsigned char fostered_address_figure[] =
        "<template><table><address>x</address><tr><td>y"
        "</td></tr></table><table><tbody><figure>z</figure>"
        "<tr><td>w</td></tr></tbody></table></template>";
    static const unsigned char direct_row_fostered_blocks[] =
        "<template><tr><blockquote><span>x<td>y</td></template>"
        "<template><tr><dl><dt>a<td>z</td></template>"
        "<template><tr><legend>x<td>y</template>";
    static const unsigned char direct_row_address_figure[] =
        "<template><tr><address>x<td>y</td></template>"
        "<template><tr><figure>x<tr><td>y</td></template>";
    static const unsigned char direct_section_fostered_rows[] =
        "<template><tbody><article>x<tr><td>y</template>"
        "<template><tbody><div><span>x<td>y</template>";
    static const unsigned char direct_section_fostered_parts[] =
        "<template><tbody><article>x<thead><tr><td>y</template>"
        "<template><tbody><div><span>x<colgroup><col></template>"
        "<template><tbody><article>x<caption>y</template>";
    static const unsigned char direct_section_row_fostered[] =
        "<template><tbody><tr><div>x<thead><tr><td>y</template>"
        "<template><tbody><tr><article>x<td>y<tr><td>z</template>"
        "<template><tbody><tr><span>x<colgroup><col></template>";
    static const unsigned char direct_row_transition[] =
        "<template><tr><tr><td>a</td><tr><blockquote>"
        "<span>x<tr><td>b</td></template>";
    static const unsigned char adjacent_tables[] =
        "<head><template><table><col><table><td>x</template>";
    static const unsigned char table_select_reprocess[] =
        "<head><template><table><select><option>A<input type=text>"
        "<select><option>B<textarea>\nC&amp;D</textarea>"
        "<xmp><b>raw</b></xmp></br><tr><td>E</template>";
    static const unsigned char caption_select_table[] =
        "<template><caption><select><option>x<table>y</template>";
    static const unsigned char body_fostered_div_row[] =
        "<table><div>x<tr><td>y</table>";
    static const unsigned char body_fostered_section_span[] =
        "<table><section>x<tr><td>y</table>"
        "<table><span>a<td>b</table>";
    static const unsigned char body_fostered_blocks[] =
        "<table><blockquote>x<tr><td>y</table>"
        "<table><p>a<td>b</table>";
    static const unsigned char body_table_select_textarea[] =
        "<table><colgroup><select><option>x<textarea>\ny&amp;z"
        "</textarea><tr><td>q";
    static const unsigned char body_table_text_modes[] =
        "<table><colgroup><title>A&amp;B</title><tr><td>x</table>"
        "<table><tbody><xmp><b>raw</b></xmp><tr><td>y</table>"
        "<table><iframe>Z</iframe><noembed>Q</noembed>"
        "<tr><td>z</table><table><colgroup><noframes><b>raw</b>"
        "</noframes><tr><td>w</table><table><plaintext>tail";
    static const unsigned char select_closed_by_input[] =
        "<template><table><select><option>a<input type=text>y"
        "</select><tr><td>x</td></tr></table></template>";
    static const unsigned char select_closed_by_keygen[] =
        "<template><table><select><option>x<keygen>y"
        "<tr><td>z</template><select><option>a<keygen>b";
    static const unsigned char orphan_option_body[] =
        "<select><option>a<select><optgroup><option>b<p>c"
        "<template><select><option>x<select><option>y<p>z</template>";
    static const unsigned char fostered_table_start[] =
        "<template><table><tbody><div>x<table><td>y</template>"
        "<template><table><colgroup><object>x<table><td>z</template>"
        "<template><tbody><div>x<table><td>y</template>"
        "<template><tr><object>x<table>x</template>";
    static const unsigned char table_plaintext[] =
        "<head><template><table><plaintext>raw</table></template>";
    static const unsigned char unclosed_foster[] =
        "<head><template><table><li>one<tr><td>A</td>"
        "<span>two<col><tr><td>B</template>";
    static const unsigned char direct_row[] =
        "<body><template><tr><td>A</td></tr><template></template>"
        "<td>B</td></template>";
    static const unsigned char direct_section[] =
        "<body><template><thead><tr><td>A</td></tr></thead>"
        "<template><tr></tr></template><tr><td>B</td>"
        "<tfoot><td>C</td></template>";
    static const unsigned char direct_columns[] =
        "<body><template><col>A \tB<template></template>"
        "<html data-x=y><colgroup><div><col></template>";
    static const unsigned char direct_group_content[] =
        "<template><html><tr><td>x</template>"
        "<template><head><tr><td>x</template>"
        "<template><body><tr><td>x</template>"
        "<template><frameset><tr><td>x</template>"
        "<template><frame><tr><td>x</template>"
        "<template><table><template><frame><tr><td>x</template><tr><td>y</template>"
        "<template><colgroup><noframes>x</noframes><col>"
        "<tr><td>y</template><template><colgroup><object>x</object>"
        "<col><tr><td>z</template>"
        "<template><colgroup><body><col></template>"
        "<template><table><colgroup><frame><col></template>";
    static const unsigned char column_after_fostered[] =
        "<template><colgroup><div>x<col>y</template>"
        "<template><tbody><object>x<col>y</template>"
        "<template><table></table><div>x<col>y</template>";
    static const unsigned char direct_part_after_fostered[] =
        "<template><colgroup><div>x<td>y</template>"
        "<template><caption><object>x<tr><td>z</template>"
        "<template><tbody><div>x<td>w</template>";
    static const unsigned char direct_table_start[] =
        "<template><colgroup><div>x<table><td>y</template>"
        "<template><caption></caption><object>x<table><td>z"
        "</template><template><caption><div>x<table><td>w</template>";
    static const unsigned char integration_objects[] =
        "<template><math><mtext><object>x</object></mtext></math>"
        "<svg><foreignObject><object>y</object></foreignObject></svg>"
        "</template>";
    static const unsigned char nested_math_text[] =
        "<template><math><mtext><math><mi>x</mi></math>"
        "<mglyph>y</mglyph><malignmark>z</malignmark></mtext>"
        "</math></template>";
    static const unsigned char cross_foreign_roots[] =
        "<svg><g><math><mi>x</mi></math></g></svg>"
        "<math><mrow><svg><g>z</g></svg></mrow></math>"
        "<template><svg><foreignObject><svg><g>y</g></svg>"
        "</foreignObject></svg></template>";
    static const unsigned char void_body_mode[] =
        "<body><template><image src=/x><template></template>"
        "<tr><table><td>x</table></template>";
    static const unsigned char text_body_mode[] =
        "<body><template><textarea>x</textarea><template></template>"
        "<tr><table><td>y</table></template>";
    static const unsigned char crossed_menu[] =
        "<body><template><i class=x><menu>Foo</i>Bar</template>";
    static const unsigned char crossed_list[] =
        "<body><template><strong><ol>Foo</strong>Bar</template>";
    static const unsigned char crossed_open_list_item[] =
        "<template><b class=x><ul><li>x</b>y</li></ul></template>";
    static const unsigned char crossed_multi_list_item[] =
        "<template><b><ul><li>one</li><li>two</li>"
        "<li>three</b>four</li></ul></template>";
    static const unsigned char crossed_formatted_list_item[] =
        "<template><b class=x><ul><li><em id=z>x</b>y</em>"
        "</li></ul></template>";
    static const unsigned char crossed_nested_list_item[] =
        "<template><b class=x><ul><li><em id=e><strong id=s>"
        "x</b>y</strong></em></li></ul></template>";
    static const unsigned char crossed_span_list_item[] =
        "<template><b class=x><ul><li><span title=t>"
        "x</b>y</span></li></ul></template>";
    static const unsigned char crossed_nested_spans[] =
        "<template><b class=x><ul><li><span title=t><span>"
        "<em id=e>x</b>y</em></span></span></li></ul></template>";
    static const unsigned char crossed_div_list_item[] =
        "<template><b class=x><ul><li>prefix<div id=d>"
        "x</b>y</div></li></ul></template>";
    static const unsigned char crossed_paragraph_list_item[] =
        "<template><b><ul><li><p id=x>before</b>after</p>"
        "</li></ul></template>";
    static const unsigned char crossed_div_span[] =
        "<template><b class=x><ul><li><div id=d><span title=t>"
        "x</b>y</span></div></li></ul></template>";
    static const unsigned char crossed_div_formatting[] =
        "<template><b class=x><ul><li><div id=d><span>"
        "<em id=e>x</b>y</em></span></div></li></ul></template>";
    static const unsigned char crossed_span_div[] =
        "<template><b class=x><ul><li><span title=t>"
        "<div id=d>x</b>y</div></span></li></ul></template>";
    static const unsigned char crossed_inline_containers[] =
        "<template><b><ul><li><label>x</b>y</label>"
        "</li></ul></template>"
        "<template><b><ul><li><div><ruby>x</b>y</ruby>"
        "</div></li></ul></template>";
    static const unsigned char crossed_quote_containers[] =
        "<template><b><ul><li><q>x</b>y</q></li>"
        "</ul></template><template><b><ul><li>"
        "<cite><abbr>x</b>y</abbr></cite></li></ul></template>"
        "<template><b><ul><li><div><q>x</b>y</q>"
        "</div></li></ul></template>";
    static const unsigned char crossed_nested_block[] =
        "<template><b class=x><ul><li><div id=d>"
        "<section title=s>x</b>y</section></div></li></ul></template>";
    static const unsigned char crossed_two_blocks[] =
        "<template><b><ul><li><section><div>x</b>y"
        "</div></section></li></ul></template>"
        "<template><b><ul><li><div><p>x</b>y</p>"
        "</div></li></ul></template>"
        "<template><b><ul><li><section><section>x</b>y"
        "</section></section></li></ul></template>";
    static const unsigned char crossed_three_blocks[] =
        "<template><b><ul><li><div><section><div>x</b>y"
        "</div></section></div></li></ul></template>";
    static const unsigned char crossed_deep_blocks[] =
        "<template><b><ul><li><div><div><div><div><div>x"
        "</b>y</div></div></div></div></div></li></ul></template>";
    static const unsigned char crossed_nested_inline[] =
        "<template><b><ul><li><div><section><label><span>"
        "x</b>y</span></label></section></div></li></ul></template>";
    static const unsigned char crossed_surrounding_inline[] =
        "<template><b><ul><li><label><span><div><section>"
        "<span>x</b>y</span></section></div></span></label>"
        "</li></ul></template>";
    static const unsigned char crossed_nested_formatting[] =
        "<template><b><ul><li><span><div><section>"
        "<em><i>x</b>y</i></em></section></div></span>"
        "</li></ul></template>";
    static const unsigned char crossed_interposed_inline[] =
        "<template><b><ul><li><div><span><section><div>"
        "x</b>y</div></section></span></div></li></ul>"
        "</template><template><b><ul><li><div><label>"
        "<section><em>x</b>y</em></section></label>"
        "</div></li></ul></template>";
    static const unsigned char crossed_both_inline[] =
        "<template><b><ul><li><span><div><label><section>"
        "<span><em>x</b>y</em></span></section></label>"
        "</div></span></li></ul></template>";
    static const unsigned char crossed_nested_gap[] =
        "<template><b><ul><li><div><label><section>"
        "<span><div><em>x</b>y</em></div></span>"
        "</section></label></div></li></ul></template>";
    static const unsigned char crossed_two_gaps[] =
        "<template><b><ul><li><div><section><span><div>"
        "<label><section>x</b>y</section></label></div>"
        "</span></section></div></li></ul></template>";
    static const unsigned char colgroup_formatting[] =
        "<template><table><colgroup><b class=x><em id=y>text</em>"
        "</b><col></table></template>";
    static const unsigned char colgroup_foreign[] =
        "<template><b><i><table><colgroup><input href=x>x</template>"
        "<template><b><table><colgroup><wbr><img src=x><image src=y> z</template>"
        "<template><b><table><colgroup><input type=hidden> x</template>"
        "<template><b><table><colgroup><param>x</template>"
        "<table><colgroup> <head> \n<col></table>"
        "<table><colgroup><body class=x> <col></table>"
        "<table><colgroup><frameset> <col><frame> <col></table>"
        "<table><colgroup><html lang=x> <col></table>"
        "<template><svg><colgroup></dl><!--c--><?x y?></template>"
        "<template><math><colgroup></div><!--c--></template>"
        "<table><thead><math><mtext><table>x<tr><td>y</table>"
        "<table><tbody><svg><foreignObject><table>x<tr><td>y</table>"
        "<svg></body><circle/><g/><!--x--><?x y?><![CDATA[z]]></svg>"
        "<math></html><mrow/><!--x--></math>"
        "<svg><g></body></g><circle/></svg>"
        "<svg></html><foreignObject><p>x</p></foreignObject></svg>"
        "<svg></body><p>x</p>"
        "<table><colgroup><math><mi>x</mi></math> y<col></table>"
        "<table><colgroup><svg><g>x</g></svg> y<col></table>"
        "<template><table><colgroup><svg><g>x</g></svg>"
        "<col></table></template>"
        "<template><table><colgroup><math><mi>y</mi></math>"
        "<col></table></template>";
    static const unsigned char table_foreign[] =
        "<template><table><svg><g>x</g></svg><tr><td>y"
        "</table></template>"
        "<template><table><tbody><math><mi>x</mi></math>"
        "<tr><td>y</table></template>"
        "<template><table><tbody><tr><svg><g>x</g></svg>"
        "<td>y</table></template>";
    static const unsigned char table_inline[] =
        "<template><table><colgroup><ruby>x<rt>y</ruby>"
        "<col></table></template>"
        "<template><table><tbody><tr><cite>x</cite>"
        "<td>y</table></template>";
    static const unsigned char table_dialogs[] =
        "<template><table><colgroup><dialog>x</dialog>"
        "<col></table></template>"
        "<template><table><tbody><tr><dialog><p>y</p>"
        "</dialog><td>z</table></template>";
    static const unsigned char template_details[] =
        "<template><details><summary>x</summary><p>y</p>"
        "</details></template><template><table><colgroup>"
        "<details><summary>a</summary></details><col>"
        "</table></template><template><table><tbody><tr>"
        "<summary>b</summary><td>c</table></template>";
    static const unsigned char template_fieldsets[] =
        "<template><fieldset><legend>x</legend><p>y</p>"
        "</fieldset></template><template><table><colgroup>"
        "<fieldset><legend>a</legend></fieldset><col>"
        "</table></template><template><table><tbody><tr>"
        "<legend>b</legend><td>c</table></template>";
    static const unsigned char template_figcaptions[] =
        "<template><figure><figcaption>x</figcaption><p>y</p>"
        "</figure></template><template><table><colgroup>"
        "<figcaption>a</figcaption><col></table></template>"
        "<template><table><tbody><tr><figure><figcaption>b"
        "</figcaption></figure><td>c</table></template>";
    static const unsigned char template_more_blocks[] =
        "<template><table><colgroup><hgroup><h2>x</h2>"
        "</hgroup><col></table></template><template><table>"
        "<tbody><tr><search><p>y</p></search><td>z"
        "</table></template><template><table><colgroup>"
        "<dir><li>a<li>b</dir><col></table></template>";
    static const unsigned char template_noscript[] =
        "<template><table><colgroup><noscript><b>x</b>"
        "</noscript><col></table></template><template><table>"
        "<tbody><tr><noscript><b>x</b></noscript><td>y"
        "</table></template>";
    static const unsigned char template_media[] =
        "<template><table><colgroup><picture><source src=x>"
        "<img src=y></picture><col></table></template>"
        "<template><table><tbody><tr><video><source src=x>v"
        "</video><td>y</table></template><template><table>"
        "<colgroup><canvas><span>x</span></canvas><col>"
        "</table></template><template><table><colgroup>"
        "<audio><source src=x>a</audio><col></table></template>";
    static const unsigned char template_phrasing[] =
        "<template><data value=a><dfn>x</dfn><mark>y</mark>"
        "<time datetime=z>t</time></data></template>"
        "<template><table><colgroup><del><samp>a</samp>"
        "<kbd>b</kbd></del><col></table></template>";
    static const unsigned char template_values[] =
        "<template><output name=x><span>a</span></output>"
        "</template><template><table><colgroup><progress"
        " value=1><span>b</span></progress><col></table>"
        "</template><template><table><tbody><tr><meter"
        " value=2><span>c</span></meter><td>d</table></template>";
    static const unsigned char crossed_long_gaps[] =
        "<template><b><ul><li><span><div><span><section>"
        "<span><div><span><p>x</b>y</p></div></section>"
        "</div></li></ul></template>";
    static const unsigned char template_maps[] =
        "<template><map name=m><area href=/x><span>a</span>"
        "</map></template><template><table><colgroup>"
        "<map name=m><area href=/x><span>a</span></map>"
        "<col></table></template><template><table><tbody>"
        "<tr><map name=m><area href=/x>x</map><td>y"
        "</table></template>";
    static const unsigned char template_datalists[] =
        "<template><datalist id=d><option>a<option>b"
        "</datalist></template><template><table><colgroup>"
        "<datalist id=d><option>a<option>b</datalist><col>"
        "</table></template><template><table><tbody><tr>"
        "<datalist id=d><option>a<option>b</datalist>"
        "<td>y</table></template>";
    static const unsigned char svg_descendants[] =
        "<template><svg><g><slot id=s><video>x</video>"
        "</slot><select><option>a</option></select>"
        "<circle/></g></svg></template>";
    static const unsigned char math_breakouts[] =
        "<template><math><mrow><slot>x</slot><video>y"
        "</video></mrow></math></template>"
        "<template><math><mrow><strong>a</strong>"
        "<font color=red>b</font></mrow></math></template>"
        "<template><math><mrow><table>c</table></mrow>"
        "</math></template>";
    static const unsigned char math_nested_foreign[] =
        "<template><math><mrow><select><option>x</option>"
        "</select><slot>y</slot></mrow></math></template>"
        "<template><math><mrow><slot><strong>z</strong>"
        "</slot></mrow></math></template>"
        "<template><math><msup><table>x</table></msup>"
        "</math></template>";
    static const unsigned char svg_nested_breakouts[] =
        "<template><svg><g><slot><p>x</p></slot></g>"
        "</svg></template><template><svg><g><slot>"
        "<font color=red>y</font></slot></g></svg>"
        "</template><template><svg><g><slot><svg>"
        "<g><p>z</p></g></svg></slot></g></svg></template>";
    static const unsigned char svg_integrated_containers[] =
        "<template><svg><foreignObject><g><p>x</p></g>"
        "<video><span>y</span></video></foreignObject>"
        "<desc><custom><b>z</b></custom></desc>"
        "</svg></template>";
    static const unsigned char svg_integrated_text[] =
        "<template><svg><foreignObject><title><b>x</b>"
        "</title><script><b>y</b></script></foreignObject>"
        "<desc><textarea>\n<b>z</b></textarea></desc>"
        "<title><xmp><b>w</b></xmp></title></svg></template>";
    static const unsigned char math_integrated_content[] =
        "<template><math><mtext><script><b>x</b></script>"
        "<noscript><span>y</span></noscript><custom>"
        "<b>z</b></custom></mtext><annotation-xml"
        " encoding=text/html><iframe><b>w</b></iframe>"
        "</annotation-xml></math></template>";
    static const unsigned char svg_math_breakouts[] =
        "<template><math><mtext><svg><g><p>x</p></g>"
        "</svg></mtext></math></template><template><math>"
        "<mrow><svg><g><p>y</p></g></svg></mrow>"
        "</math></template><template><math><annotation-xml"
        " encoding=text/html><svg><g><p>z</p></g></svg>"
        "</annotation-xml></math></template>";
    static const unsigned char template_transparent[] =
        "<template><ins><span>x</span></ins><slot><b>y"
        "</b></slot></template><template><table><colgroup>"
        "<slot><ins><span>x</span></ins></slot><col>"
        "</table></template><template><table><tbody><tr>"
        "<ins><slot>x</slot></ins><td>y</table></template>";
    static const unsigned char template_custom_foster[] =
        "<template><table><colgroup><my-widget a=1><b>x"
        "</b></my-widget><col></table></template>"
        "<template><table><tbody><tr><custom-box>x"
        "</custom-box><td>y</table></template>"
        "<template><table><unknown>x</unknown><tr>"
        "<td>z</table></template>";
    static const unsigned char crossed_foster_order[] =
        "<template><table><colgroup><strong><ul><li>x"
        "</strong>y</li></ul><col></table></template>"
        "<template><table><tbody><tr><b><ol><li>"
        "<div>a</b>z</div></li></ol><td>q</table></template>";
    static const unsigned char crossed_list_trailing_inline[] =
        "<template><b><ul><li><em class=x><span>a</b>b</template>";
    static const unsigned char crossed_list_interposed_formatting[] =
        "<template><b><ul><li><em class=x><span><strong title=t>x"
        "</b>y</template>";
    static const unsigned char crossed_list_formatted_block[] =
        "<template><b><ul><li><em class=x><div><span><q>a"
        "</b>b</template>";
    static const unsigned char crossed_list_formatting_chain[] =
        "<template><b><ul><li><em class=x><strong title=t>"
        "<i data-q=v><div><span>a</b>b</template>";
    static const unsigned char crossed_list_long_formatting_chain[] =
        "<template><b><ul><li><em><strong><i><code><em class=x>"
        "<div><span>a</b>b</template>";
    static const unsigned char crossed_list_preblock_inline[] =
        "<template><b><ul><li><em class=x><strong title=t>"
        "<i id=z><code class=c><q><div>a</b>b</template>";
    static const unsigned char crossed_list_interposed_inline[] =
        "<template><b><ul><li><em class=x><span id=s>"
        "<strong title=t><div>a</b>b</template>";
    static const unsigned char crossed_list_closure[] =
        "<template><b><ul><li><em class=x><div>a</b>b</ul>"
        "<template>q</template>c</em>d</template>";
    static const unsigned char following_list_block[] =
        "<template><ul><li><em class=x>a</ul><p>b</template>"
        "<template><ol><li><strong>c</ol><div>d</template>";
    static const unsigned char closed_list_block[] =
        "<template><ul><li><em class=x>a</ul><p>b</p>c"
        "</template><template><ol><li><strong>d</ol>"
        "<div>e</div>f</template>";
    static const unsigned char following_list_section[] =
        "<template><ul><li><em class=x>a</ul><section>b</section>c"
        "</template><template><ol><li><strong>d</ol>"
        "<article>e</article>f</template>";
    static const unsigned char following_list_heading[] =
        "<template><ul><li><em>a</ul><h1>b</h1>c"
        "</template><template><ol><li><strong>d</ol>"
        "<h6>e</h6>f</template>";
    static const unsigned char following_list_void[] =
        "<template><ul><li><em class=x>a</ul><br>b</em>c"
        "</template><template><ol><li><strong>d</ol>"
        "<img src=x>e</template>";
    static const unsigned char following_list_void_chain[] =
        "<template><ul><li><em>a</ul><br><img src=x><wbr>b</em>c"
        "</template>";
    static const unsigned char following_list_rule[] =
        "<template><ul><li><em>a</ul><hr><hr>b</template>"
        "<template><ol><li><strong>c</ol><p>d</p><hr>e</template>";
    static const unsigned char following_list_item[] =
        "<template><ul><li><em class=x>a</ul><ol><li>b</ol>c"
        "</template><template><ol><li><strong>d</ol>"
        "<ul><li></ul>e</template>";
    static const unsigned char following_list_siblings[] =
        "<template><ul><li><em>a</ul><ol><li>b<li>c</ol>d"
        "</template><template><ol><li><strong>e</ol>"
        "<ul><li>f</li><li>g</li></ul>h</template>";
    static const unsigned char nested_list_continuation[] =
        "<template><ul><li><em class=x>a</ul><div>"
        "<ol><li>b<li>c</ol>d</div></template>"
        "<template><ol><li><strong>e</ol><div>"
        "<ul><li>f</li><li>g</li></ul>h</div></template>";
    static const unsigned char following_list_spans[] =
        "<template><ul><li><em class=x>a</ul>"
        "<span>b</span><span>c</span>d</em>e</template>";
    static const unsigned char crossed_following_span[] =
        "<template><ul><li><em class=x>a</ul><span>b</em>c"
        "</template><template><ol><li><strong>d</ol>"
        "<span>e</strong></span>f</template>";
    static const unsigned char following_span_voids[] =
        "<template><ul><li><em class=x>a</ul>"
        "<span><br><img src=x><wbr>b</span>c</template>";
    static const unsigned char rule_after_formatting[] =
        "<template><ul><li><em>a</ul><br><hr>b</template>"
        "<template><ol><li><strong>c</ol>"
        "<span>d</span><hr>e</template>";
    static const unsigned char blocks_inside_formatting[] =
        "<template><ul><li><em>a</ul><br><p>b</p>c</template>"
        "<template><ol><li><strong>d</ol>"
        "<span>e</span><div>f</div>g</template>";
    static const unsigned char nested_formatting_spans[] =
        "<template><ul><li><em class=x>a</ul>"
        "<span><span><span>b</span>c</span>d</span>e</template>"
        "<template><ol><li><strong>f</ol>"
        "<span><span>g</strong>h</template>";
    static const unsigned char nested_span_voids[] =
        "<template><ul><li><em>a</ul><span><span>b<hr>c"
        "</span></span></template><template><ol><li>"
        "<strong>d</ol><span><span>e<br><img src=x>"
        "<wbr>f</span></span></template>";
    static const unsigned char blocks_in_nested_spans[] =
        "<template><ul><li><em class=x>a</ul><span>"
        "<span>b<div>c</div>d</span></span>e</template>"
        "<template><ol><li><strong>f</ol><span>"
        "<span>g<section>h</section>i</span></span></template>";
    static const unsigned char deep_list_continuation[] =
        "<template><ul><li><em class=x>a</ul><div><div>"
        "<div><ol><li>b<li>c</ol>d</div>e</div>f</div>g</template>";
    static const unsigned char following_special_blocks[] =
        "<template><ul><li><em class=x>a</ul>"
        "<address>b</address>c<br><blockquote>d</blockquote>"
        "e<span><figure>f</figure>g</span></template>";
    static const unsigned char objects_after_lists[] =
        "<template><ul><li><em class=x>a</ul>"
        "<object data=q>b</object>c</template>"
        "<template><ol><li><strong>d</ol>"
        "<br><object>e</object>f</template>";
    static const unsigned char nested_following_lists[] =
        "<template><ul><li><em class=x>a</ul>"
        "<ol><li><ul><li><ol><li>b</ol>c</ul>d</ol>e</template>";
    static const unsigned char formatted_nested_list[] =
        "<template><ul><li><em class=x>a</ul><ol>"
        "<li>b<ul><li>c<li>d</ul>e</ol>f</template>";
    static const unsigned char list_div_descendants[] =
        "<template><ul><li><em class=x>a</ul>"
        "<ol><li><div><div><ul><li>b</ul>c</div>d"
        "</div>e</ol>f</template>";
    static const unsigned char formatted_list_divs[] =
        "<template><ul><li><em class=x>a</ul>"
        "<ol><li>b<div><div><ul><li>c</ul>d</div>e"
        "</div>f</ol>g</template>";
    static const unsigned char formatted_list_blocks[] =
        "<template><ul><li><em class=x>a</ul>"
        "<ol><li>b<section><blockquote><ul><li>c</ul>d"
        "</blockquote>e</section>f</ol>g</template>";
    static const unsigned char markers_after_lists[] =
        "<template><ul><li><em class=x>a</ul>"
        "<marquee>b</marquee>c<br><applet>d</applet>e</template>";
    static const unsigned char text_modes_after_list[] =
        "<template><ul><li><em class=x>a</ul>"
        "<title>b&amp;c</title><textarea>d&amp;e</textarea>"
        "<script>f()</script><style>g{}</style>h</template>";
    static const unsigned char raw_modes_after_list[] =
        "<template><ul><li><em class=x>a</ul><xmp>b</xmp>c"
        "</template><template><ol><li><strong>d</ol>"
        "<iframe>e</iframe><noembed>f</noembed>"
        "<noframes>g</noframes>h</template>";
    static const unsigned char plaintext_after_list[] =
        "<template><ul><li><em class=x>a</ul><plaintext>b<b>c";
    static const unsigned char following_preformatted[] =
        "<template><ul><li><em class=x>a</ul><pre>\nb</pre>c"
        "</template><template><ol><li><strong>d</ol>"
        "<br><listing>\ne</listing>f</template>";
    static const unsigned char foreign_after_list[] =
        "<template><ul><li><em class=x>a</ul>"
        "<svg><circle/></svg>b</template>"
        "<template><ol><li><strong>c</ol>"
        "<math><mi>d</mi></math>e</template>";
    static const unsigned char foreign_under_formatting[] =
        "<template><ul><li><em class=x>a</ul>"
        "<br><svg><circle/></svg>b</template>"
        "<template><ol><li><strong>c</ol><span>"
        "<math><mi>d</mi></math>e</span></template>";
    static const unsigned char foreign_descendants_after_list[] =
        "<template><ul><li><em class=x>a</ul>"
        "<svg><g><path/></g></svg>b</template>"
        "<template><ol><li><strong>c</ol>"
        "<math><mo>+</mo><mn>2</mn></math>d</template>";
    static const unsigned char multiple_list_formatting[] =
        "<template><ul><li><em class=x><strong title=t>"
        "<i>a</ul><template>q</template>b</template>"
        "<template><ol><li><b><em>c</ol>d</template>";
    static const unsigned char multiple_formatting_spans[] =
        "<template><ul><li><em class=x><strong title=t>x</ul>"
        "<hr><span><span><img src=a>y</span>z</span><br>w"
        "<span>q</span><wbr><hr></template>";
    static const unsigned char multiple_formatting_div[] =
        "<template><ul><li><em class=x><strong title=t>x</ul>"
        "<p>a<section>y<blockquote>z</blockquote>q</section>"
        "<div>v</div>w</template>";
    static const unsigned char multiple_formatting_ends[] =
        "<template><ul><li><em class=x><strong title=t><i>a</ul>"
        "b</i>c</strong>d</em>e</template>"
        "<template><ol><li><em><strong>x</ol></strong>y</template>"
        "<template><ul><li><em><strong>x</ul>"
        "<div>y</strong>z</div>w</template>"
        "<template><ul><li><em class=x><strong><i>x</ul>"
        "y</em>z</i>q</strong>w</template>";
    static const unsigned char multiple_formatting_middle[] =
        "<template><ul><li><em><strong><i>x</ul>"
        "y</strong>z</i>w</em>q</template>"
        "<template><ol><li><em><em><strong>x</ol>"
        "y</em>z</strong>w</em>q</template>";
    static const unsigned char multiple_formatting_span_end[] =
        "<template><ul><li><em class=x><strong>x</ul>"
        "<span>y</em>z</span>w</template>";
    static const unsigned char multiple_formatting_block_end[] =
        "<template><ul><li><em class=x><strong><i>x</ul>"
        "<div>y</em>z</div>w</template>";
    static const unsigned char multiple_formatting_block_span_end[] =
        "<template><ul><li><em class=x><strong>x</ul>"
        "<div><span>y</em>z</span></div>w</template>";
    static const unsigned char multiple_formatting_nested_block_end[] =
        "<template><ul><li><em class=x><strong><i>x</ul>"
        "<div><section><blockquote>y</em>z</blockquote>"
        "</section></div>w</template>";
    static const unsigned char multiple_formatting_article[] =
        "<template><ul><li><em class=x><strong title=t>x</ul>"
        "<p>a<article>y<div>z</div>q</article>w</template>";
    static const unsigned char multiple_formatting_direct_blocks[] =
        "<template><ul><li><em><strong>x</ul><p>a<address>b"
        "</address><figure>c</figure><pre>\nd</pre>"
        "<listing>\ne</listing>f</template>";
    static const unsigned char multiple_formatting_headings[] =
        "<template><ul><li><em><strong>x</ul>"
        "<p>a<h2>y</h3>z<h4>q</h4>w</template>";
    static const unsigned char multiple_formatting_following_list[] =
        "<template><ul><li><em class=x><strong>x</ul>"
        "<ol><li>y</li><li>z</ol>w</template>";
    static const unsigned char multiple_formatting_nested_list[] =
        "<template><ul><li><em class=x><strong>x</ul>"
        "<ol><li>y<ul><li>z</ul>w</li></ol>q</template>";
    static const unsigned char multiple_formatting_outer_list_end[] =
        "<template><ul><li><em><strong>x</ul>"
        "<ol><li>y<ul><li>z</ol>w</template>";
    static const unsigned char nested_formatted_item_ends[] =
        "<template><ul><li><b><em>x<ul><li>y</li></ul>"
        "</li></ul>z</template>"
        "<template><ol><li><b><em>x<ul><li>y</li></ul>"
        "</em>z</li></ol>w</template>";
    static const unsigned char nested_crossed_formatting[] =
        "<template><ul><li><b><em><label><i><ruby><span>x"
        "<ul><li>y</li></ul></b>z</span></li></ul>w"
        "</template>";
    static const unsigned char multiple_formatting_foreign[] =
        "<template><ul><li><em class=x><strong title=t>x</ul>"
        "<svg><g><path/></g></svg>y<math><mi>q</mi></math>z"
        "</template>";
    static const unsigned char multiple_formatting_paragraphs[] =
        "<template><ul><li><em class=x><strong title=t>x</ul>"
        "<p>y<p>z<hr>w</template>";
    static const unsigned char crossed_direct_blocks[] =
        "<template><b><p>x</b>y</p></template>"
        "<template><b>a<div>x</b>y</div>z</template>"
        "<template><table><colgroup><strong><section>x"
        "</strong>y</section><col></table></template>";
    static const unsigned char crossed_sectioning[] =
        "<template><strong><article>x</strong>y</article>"
        "</template><template><table><colgroup><b><nav>x"
        "</b>y</nav><col></table></template>"
        "<template><em><aside>x</em>y</aside></template>";
    static const unsigned char crossed_special_blocks[] =
        "<template><b><blockquote>x</b>y</blockquote>"
        "</template><template><table><colgroup><strong>"
        "<fieldset>x</strong>y</fieldset><col></table>"
        "</template><template><em><figure>x</em>y</figure>"
        "</template>";
    static const unsigned char crossed_block_inline[] =
        "<template><b><p><span>x</b>y</span></p>"
        "</template><template><table><colgroup>"
        "<strong><div><q>a</strong>b</q></div><col>"
        "</table></template>";
    static const unsigned char crossed_block_formatting[] =
        "<template><b><p><strong>x</b>y</strong>z</p>"
        "</template><template><table><colgroup><b><div>"
        "<em class=x>a</b>b</em></div><col></table>"
        "</template>";
    static const unsigned char crossed_block_formatting_pair[] =
        "<template><b><div><em class=x><strong title=t>a"
        "</b>b</strong></em></div></template>"
        "<template><table><colgroup><b><section><em>"
        "<strong>x</b>y</strong></em></section><col>"
        "</table></template>";
    static const unsigned char crossed_block_three_formatting[] =
        "<template><b><div><em><strong><code>x</b>"
        "y</code></strong></em></div></template>"
        "<template><table><tbody><tr><b><section>"
        "<em><strong><i>a</b>b</i></strong></em>"
        "</section><td>c</table></template>";
    static const unsigned char crossed_block_inline_gap[] =
        "<template><b><p><span><em><strong>x</b>"
        "y</strong></em></span></p></template>"
        "<template><table><colgroup><b><section><q>"
        "<em>a</b>b</em></q></section><col></table>"
        "</template><template><b><div><span><q><em>"
        "<strong><code>x</b>y</code></strong></em>"
        "</q></span></div></template>";
    static const unsigned char crossed_formatting_trailing_inline[] =
        "<template><b><div><em class=x><span><q>a</b>b</template>";
    static const unsigned char crossed_interposed_formatting[] =
        "<template><b><div><em class=x><span><strong title=t>x"
        "</b>y</template>";
    static const unsigned char integrated_templates[] =
        "<template><svg><foreignObject><template><div>x"
        "</div></template><p>y</p></foreignObject><desc>"
        "<template><b>z</b></template></desc></svg>"
        "</template><template><math><mtext><template>"
        "<ul><li>a</ul></template></mtext></math></template>";
    static const unsigned char nested_objects[] =
        "<template><object><object>x</object></object>"
        "</template><template><table><object><select><option>a"
        "</select><object>b</object></object><tr><td>c"
        "</table></template>";
    static const unsigned char template_buttons[] =
        "<template><table><colgroup><button><span>a"
        "<button>b</button><col></table></template>"
        "<template><div><button>a<button>b</button>"
        "</div></template>";
    static const unsigned char table_orphan_options[] =
        "<template><table><colgroup><select><option>a"
        "<select><option>b</select><col></table></template>"
        "<template><table><optgroup><option>a<option>b"
        "<optgroup><option>c<tr><td>d</table></template>";
    static const unsigned char svg_buttons[] =
        "<template><svg><foreignObject><button>x</button>"
        "</foreignObject><g><button>y</button></g>"
        "<desc><button>a<button>b</button></desc>"
        "</svg></template>";
    static const unsigned char math_buttons[] =
        "<template><math><mtext><button>a<button>b</button>"
        "</mtext><mrow><button>c</button></mrow>"
        "<annotation-xml encoding=text/html><button>d</button>"
        "</annotation-xml></math></template>";
    static const unsigned char svg_integration_blocks[] =
        "<template><svg><foreignObject><ul><li>a<li>b</ul>"
        "</foreignObject><title><ol><li>x</li></ol></title>"
        "<desc><section>y</section></desc></svg></template>";
    static const unsigned char math_integration_lists[] =
        "<template><math><mtext><ul><li>a<li>b</ul>"
        "<section>c</section></mtext><annotation-xml "
        "encoding=text/html><ol><li>d</li></ol></annotation-xml>"
        "</math><math><mrow><ol><li>e</li></ol></mrow>"
        "</math></template>";
    static const unsigned char foreign_articles[] =
        "<template><svg><g><article>x</article></g>"
        "<foreignObject><article>y</article></foreignObject>"
        "</svg><math><mtext><article>z</article></mtext>"
        "</math></template>";
    static const unsigned char foreign_sectioning[] =
        "<template><svg><g><aside>x</aside><main>y</main></g>"
        "<foreignObject><nav>z</nav></foreignObject></svg>"
        "<math><mrow><header>a</header></mrow><mtext>"
        "<footer>b</footer></mtext></math></template>";
    static const unsigned char foreign_simple_inline[] =
        "<template><svg><g><label>x</label><cite>y</cite></g>"
        "<foreignObject><label>a</label><q>b</q>"
        "</foreignObject></svg><math><mrow><abbr>x</abbr>"
        "<q>y</q></mrow><mtext><label>a</label>"
        "<cite>b</cite></mtext></math></template>";
    static const unsigned char colgroup_fostered_heading[] =
        "<template><table><colgroup><h2>x</h2><col></table>"
        "<table><colgroup><menu><li>y</li></menu><col>"
        "</table></template>";
    static const unsigned char colgroup_fostered_pre[] =
        "<template><table><colgroup><pre>\nx</pre><col></table>"
        "<table><colgroup><listing>\ny</listing><col>"
        "</table></template>";
    static const unsigned char colgroup_text_modes[] =
        "<template><table><colgroup><style>x</style><col>"
        "</table><table><colgroup><script>y</script>"
        "</colgroup><col></table></template>";
    static const unsigned char column_after_descendants[] =
        "<template><table><tbody><tr><td><div>x<colgroup><col>"
        "</template><template><table><caption><div>y<col></template>";
    static const unsigned char column_after_math[] =
        "<template><table><colgroup><math><mi>x<col></template>"
        "<template><table><tbody><math><mi>y<col></template>";
    static const unsigned char foreign_table_cells[] =
        "<template><table><tbody><tr><td><math><mi>x<col></template>"
        "<template><table><caption><math><mi>y<col></template>"
        "<template><tbody><tr><th><svg><g>z</template>";
    static const unsigned char math_table_parts[] =
        "<template><table><tbody><tr><td><math><mi>x<tr><td>y"
        "</template><template><table><caption><math><mi>z"
        "<thead><tr><td>w</template>";
    static const unsigned char direct_section_math[] =
        "<template><tbody><math><mi>x<col></template>"
        "<template><tbody><tr><math><mi>y<td>z</template>"
        "<template><tbody><math><mi>w<thead><tr><td>q</template>";
    static const unsigned char direct_row_math[] =
        "<template><tr><math><mi>x<td>y</template>"
        "<template><tr><math><mi>z<tr><td>w</template>";
    static const unsigned char direct_row_cells[] =
        "<template><tr><th>x<tr><td>y</template>"
        "<template><tr><td>a<td>b</template>";
    static const unsigned char direct_row_cell_descendants[] =
        "<template><tr><td><div><span>x<tr><td>y</template>"
        "<template><tr><th><b>z<td>w</template>";
    static const unsigned char direct_row_math_parts[] =
        "<template><tr><math><mi>x<col><td>y</template>"
        "<template><tr><math><mi>z<thead><tr><td>w</template>";
    static const unsigned char direct_section_annotation[] =
        "<template><tbody><math><annotation-xml "
        "encoding=text/html>x<col></template>"
        "<template><tbody><tr><math><annotation-xml "
        "encoding=text/html>y<td>z</template>";
    static const unsigned char template_cell_forms[] =
        "<template><table><tbody><tr><td><form><span>x</form>"
        "y<td>z</template><template><table><caption><form>a"
        "<colgroup><col></template><template><tbody><tr><td>"
        "<form>b</form><td>c</template>";
    static const unsigned char template_cell_objects[] =
        "<template><tbody><tr><td><object>x</object><td>y"
        "</template><template><table><caption><div><object>"
        "z</object></div><tr><td>w</template>";
    static const unsigned char fostered_objects[] =
        "<template><table><tbody><article>x<object>y</object>"
        "<tr><td>z</template><template><tr><article>a<object>"
        "b</object><td>c</template>";
    static const unsigned char fostered_marquee_applet[] =
        "<template><tr><article>x<marquee>y</marquee><td>z"
        "</template><template><tbody><tr><applet>a<tr><td>b"
        "</template>";
    static const unsigned char direct_row_ignored_parts[] =
        "<template><td><tr></br href=x><td>y</template>"
        "<template><tr><caption></br><tr><td>y</template>"
        "<template><colgroup></colgroup></br><tbody><tr><td>y</template>"
        "<template></br><col></colgroup></br><div>x</template>"
        "<template><div></div></br></template>"
        "<template><tr><marquee>x<colgroup><col></template>"
        "<template><tr><td><div>y<caption>z</template>"
        "<template><tr><ins>w<thead><tr><td>q</template>";
    static const unsigned char direct_row_columns[] =
        "<template><tr><td>x</colgroup><col><td>y</template>"
        "<template><tr><article>a<col><td>b</template>"
        "<template><tbody><tr><td>c<col></template>";
    static const unsigned char direct_table_ends[] =
        "<template><tr><div>x</table><td>y</template>"
        "<template><tbody><tr><article>a</table><td>b</template>"
        "<template><tr><div>c</tfoot><td>d</template>"
        "<template><tr><td>e</table><td>f</template>";
    static const unsigned char direct_section_mismatched_ends[] =
        "<template><tbody><tr><div>x</thead><td>y</template>"
        "<template><thead><tr><marquee>z</tfoot><td>w</template>"
        "<template><tbody><tr><td>a</thead><td>b</template>";
    static const unsigned char table_mismatched_ends[] =
        "<template><table><tbody><tr><div>x</thead><td>y"
        "</template><template><table><thead><tr><marquee>"
        "z</tfoot><td>w</template><template><table><tbody>"
        "<tr><td>a</thead><td>b</template>";
    static const unsigned char nested_table_mismatched_ends[] =
        "<template><div><table><tbody><tr><div>x</thead>"
        "<td>y</template><template><p><table><thead><tr>"
        "<marquee>z</tfoot><td>w</template>";
    static const unsigned char table_noframes[] =
        "<template><tr><noframes>x&amp;<b>y</noframes>"
        "<td>z</template><template><table><colgroup>"
        "<noframes>w</noframes><col></template>";
    static const unsigned char keygen_void[] =
        "<body><keygen>x<template><tr><keygen>y<td>z"
        "</template><template><table><colgroup><keygen>w"
        "<col></template>";
    static const unsigned char isindex_container[] =
        "<body><isindex>x</isindex><template><table>"
        "<isindex>a<tr><td>b</template>";
    static const unsigned char legacy_void_head_table[] =
        "<head><basefont></head><body><bgsound>x"
        "<template><table><basefont>a<bgsound>b<tr><td>c</template>";
    static const unsigned char closed_head_noscript[] =
        "<head><noscript><basefont>x</noscript><p>y</p>"
        "<noscript>z</noscript></noscript>";
    static const unsigned char legacy_containers[] =
        "<body><menuitem>x<span>y</span></menuitem>"
        "<multicol>z</multicol><nextid>a</nextid>"
        "<template><table><spacer>b<tr><td>c</template>";
    static const unsigned char fostered_table_metadata[] =
        "<template><table><base href=/a><col></table>"
        "<table><colgroup><link href=/b><meta name=x><col>"
        "</table></template>";
    static const unsigned char fostered_table_titles[] =
        "<template><table><title>a&amp;b</title><col></table>"
        "<table><colgroup><title>c&amp;d</title><col></table>"
        "<table><tr><title>e&amp;f</title><td>z</td></tr>"
        "</table></template>";
    static const unsigned char paragraph_table[] =
        "<template><div><p>before<table>abc<td>x</table>"
        "after</p></div></template>";
    static const unsigned char colgroup_form[] =
        "<template><table><colgroup><form id=ignored><col>"
        "</table></template>";
    static const unsigned char body_descendant[] =
        "<html lang=old><div></body><html lang=new dir=rtl><!--c-->"
        "<?x y?><html title=t></div></body></html><html id=x>"
        "<!--d--><?z q?><script><b></script>"
        "<body><template><div><tr><td></td><table><tr><td>x"
        "</table></div></template>";
    static const unsigned char direct_row_foster[] =
        "<body><template><tr><div></div><input type=text>"
        "</tr></template>";
    static const unsigned char direct_row_text[] =
        "<head><template><tr> a</tr><td>b</td></template>";
    static const unsigned char fostered_row_cell[] =
        "<body><template><tr><div><span>y<td>x</td>"
        "</tr></template>";
    int status = sweep(unclosed, sizeof(unclosed) - 1, "unclosed template");
    if (status == 0)
        status = sweep(table, sizeof(table) - 1, "template table");
    if (status == 0)
        status = sweep(after_head, sizeof(after_head) - 1,
                       "after-head template");
    if (status == 0)
        status = sweep(nested_section_select, sizeof(nested_section_select) - 1,
                       "nested template section select");
    if (status == 0)
        status = sweep(foreign_template, sizeof(foreign_template) - 1,
                       "SVG template descendant");
    if (status == 0)
        status = sweep(nested_foreign_template,
                       sizeof(nested_foreign_template) - 1,
                       "SVG template in HTML template");
    if (status == 0)
        status = sweep(math_template, sizeof(math_template) - 1,
                       "MathML in HTML template");
    if (status == 0)
        status = sweep(integrated_math_template,
                       sizeof(integrated_math_template) - 1,
                       "MathML text integration in template");
    if (status == 0)
        status = sweep(math_block_template,
                       sizeof(math_block_template) - 1,
                       "MathML HTML blocks in template");
    if (status == 0)
        status = sweep(math_void_template,
                       sizeof(math_void_template) - 1,
                       "MathML HTML voids in template");
    if (status == 0)
        status = sweep(math_select_template,
                       sizeof(math_select_template) - 1,
                       "MathML HTML select in template");
    if (status == 0)
        status = sweep(math_select_input,
                       sizeof(math_select_input) - 1,
                       "MathML select input reprocessing in template");
    if (status == 0)
        status = sweep(math_text_modes,
                       sizeof(math_text_modes) - 1,
                       "MathML integrated text modes in template");
    if (status == 0)
        status = sweep(math_ruby, sizeof(math_ruby) - 1,
                       "MathML integrated ruby in template");
    if (status == 0)
        status = sweep(math_block_breakout,
                       sizeof(math_block_breakout) - 1,
                       "HTML blocks breaking out of template MathML");
    if (status == 0)
        status = sweep(math_inline_list_breakout,
                       sizeof(math_inline_list_breakout) - 1,
                       "HTML inline and list starts breaking out of MathML");
    if (status == 0)
        status = sweep(math_foreign_blocks,
                       sizeof(math_foreign_blocks) - 1,
                       "foreign MathML block names before HTML breakout");
    if (status == 0)
        status = sweep(math_annotation_template,
                       sizeof(math_annotation_template) - 1,
                       "MathML HTML annotation in template");
    if (status == 0)
        status = sweep(math_annotation_svg,
                       sizeof(math_annotation_svg) - 1,
                       "MathML SVG annotation in template");
    if (status == 0)
        status = sweep(math_descendant_svg,
                       sizeof(math_descendant_svg) - 1,
                       "SVG under MathML template descendants");
    if (status == 0)
        status = sweep(svg_breakout_template,
                       sizeof(svg_breakout_template) - 1,
                       "SVG breakout in template");
    if (status == 0)
        status = sweep(svg_foreign_section,
                       sizeof(svg_foreign_section) - 1,
                       "SVG foreign section preceding HTML breakout");
    if (status == 0)
        status = sweep(svg_inline_breakout,
                       sizeof(svg_inline_breakout) - 1,
                       "SVG inline breakout in template");
    if (status == 0)
        status = sweep(svg_sub_sup_breakout,
                       sizeof(svg_sub_sup_breakout) - 1,
                       "SVG sub and sup breakout in template");
    if (status == 0)
        status = sweep(svg_more_inline_breakout,
                       sizeof(svg_more_inline_breakout) - 1,
                       "additional SVG inline breakouts in template");
    if (status == 0)
        status = sweep(svg_embed_meta_breakout,
                       sizeof(svg_embed_meta_breakout) - 1,
                       "SVG embed and meta breakouts in template");
    if (status == 0)
        status = sweep(svg_menu_listing_breakout,
                       sizeof(svg_menu_listing_breakout) - 1,
                       "SVG menu and listing breakouts in template");
    if (status == 0)
        status = sweep(svg_font_breakout,
                       sizeof(svg_font_breakout) - 1,
                       "SVG font attribute breakout in template");
    if (status == 0)
        status = sweep(svg_body_breakout,
                       sizeof(svg_body_breakout) - 1,
                       "SVG body breakout in template");
    if (status == 0)
        status = sweep(svg_ruby_breakout,
                       sizeof(svg_ruby_breakout) - 1,
                       "SVG ruby breakout in template");
    if (status == 0)
        status = sweep(ruby_annotations,
                       sizeof(ruby_annotations) - 1,
                       "direct ruby annotations in template");
    if (status == 0)
        status = sweep(nested_ruby_annotations,
                       sizeof(nested_ruby_annotations) - 1,
                       "ruby annotations under spans in template");
    if (status == 0)
        status = sweep(nested_template_foreign,
                       sizeof(nested_template_foreign) - 1,
                       "foreign roots under template div");
    if (status == 0)
        status = sweep(svg_integrated_html,
                       sizeof(svg_integrated_html) - 1,
                       "SVG title/desc HTML integration in template");
    if (status == 0)
        status = sweep(foreign_object_html,
                       sizeof(foreign_object_html) - 1,
                       "SVG foreignObject HTML integration in template");
    if (status == 0)
        status = sweep(svg_void_breakout,
                       sizeof(svg_void_breakout) - 1,
                       "SVG void breakout in template");
    if (status == 0)
        status = sweep(svg_list_breakout,
                       sizeof(svg_list_breakout) - 1,
                       "SVG list breakout in template");
    if (status == 0)
        status = sweep(svg_block_breakout,
                       sizeof(svg_block_breakout) - 1,
                       "SVG block breakout in template");
    if (status == 0)
        status = sweep(svg_definition_breakout,
                       sizeof(svg_definition_breakout) - 1,
                       "SVG definition and block breakout in template");
    if (status == 0)
        status = sweep(integrated_foreign_template,
                       sizeof(integrated_foreign_template) - 1,
                       "SVG integration in HTML template");
    if (status == 0)
        status = sweep(crossed_anchor_paragraph,
                       sizeof(crossed_anchor_paragraph) - 1,
                       "anchor crossed by paragraph");
    if (status == 0)
        status = sweep(crossed_anchor_formatting,
                       sizeof(crossed_anchor_formatting) - 1,
                       "anchor crossed by formatting");
    if (status == 0)
        status = sweep(fostered_template_anchor,
                       sizeof(fostered_template_anchor) - 1,
                       "template fostered anchor");
    if (status == 0)
        status = sweep(outer_frameset, sizeof(outer_frameset) - 1,
                       "outer frameset");
    if (status == 0)
        status = sweep(nested_frameset, sizeof(nested_frameset) - 1,
                       "nested frameset");
    if (status == 0)
        status = sweep(virtual_foreign_recovery,
                       sizeof(virtual_foreign_recovery) - 1,
                       "virtual table foreign recovery");
    if (status == 0)
        status = sweep(option_pop_selectedcontent,
                       sizeof(option_pop_selectedcontent) - 1,
                       "option pop selectedcontent copies");
    if (status == 0)
        status = sweep(disabled_selectedcontent, sizeof(disabled_selectedcontent) - 1,
                       "disabled selectedcontent");
    if (status == 0)
        status = sweep(option_select_reset, sizeof(option_select_reset) - 1,
                       "option select reset");
    if (status == 0)
        status = sweep(frameset_noframes,
                       sizeof(frameset_noframes) - 1, "frameset noframes");
    if (status == 0)
        status = sweep(nested_template_object,
                       sizeof(nested_template_object) - 1,
                       "object in template paragraph");
    if (status == 0)
        status = sweep(template_object_select,
                       sizeof(template_object_select) - 1,
                       "select inside object and template paragraph");
    if (status == 0)
        status = sweep(formatted_template_object,
                       sizeof(formatted_template_object) - 1,
                       "object under template formatting");
    if (status == 0)
        status = sweep(block_template_object,
                       sizeof(block_template_object) - 1,
                       "object under template blocks");
    if (status == 0)
        status = sweep(table_cell_object, sizeof(table_cell_object) - 1,
                       "object in template table cell");
    if (status == 0)
        status = sweep(fostered_template_object,
                       sizeof(fostered_template_object) - 1,
                       "object fostered from template table");
    if (status == 0)
        status = sweep(section_list, sizeof(section_list) - 1,
                       "template section list");
    if (status == 0)
        status = sweep(formatted_section_list, sizeof(formatted_section_list) - 1,
                       "formatted template section list");
    if (status == 0)
        status = sweep(hidden_table_input, sizeof(hidden_table_input) - 1,
                       "hidden template table input");
    if (status == 0)
        status = sweep(fostered_table_input, sizeof(fostered_table_input) - 1,
                       "fostered template table input");
    if (status == 0)
        status = sweep(fostered_definition, sizeof(fostered_definition) - 1,
                       "fostered definition and table body transitions");
    if (status == 0)
        status = sweep(fostered_address_figure,
                       sizeof(fostered_address_figure) - 1,
                       "fostered address and figure in template tables");
    if (status == 0)
        status = sweep(direct_row_fostered_blocks,
                       sizeof(direct_row_fostered_blocks) - 1,
                       "direct rows with fostered block descendants");
    if (status == 0)
        status = sweep(direct_row_address_figure,
                       sizeof(direct_row_address_figure) - 1,
                       "direct rows with fostered address and figure");
    if (status == 0)
        status = sweep(direct_section_fostered_rows,
                       sizeof(direct_section_fostered_rows) - 1,
                       "direct sections with fostered descendants");
    if (status == 0)
        status = sweep(direct_section_fostered_parts,
                       sizeof(direct_section_fostered_parts) - 1,
                       "direct section transitions through fostered descendants");
    if (status == 0)
        status = sweep(direct_section_row_fostered,
                       sizeof(direct_section_row_fostered) - 1,
                       "direct section rows with fostered descendants");
    if (status == 0)
        status = sweep(direct_row_transition,
                       sizeof(direct_row_transition) - 1,
                       "direct row transition after fostered descendants");
    if (status == 0)
        status = sweep(adjacent_tables, sizeof(adjacent_tables) - 1,
                       "adjacent template tables");
    if (status == 0)
        status = sweep(table_select_reprocess, sizeof(table_select_reprocess) - 1,
                       "template table select reprocess");
    if (status == 0)
        status = sweep(caption_select_table, sizeof(caption_select_table) - 1,
                       "direct template caption select table start");
    if (status == 0)
        status = sweep(body_fostered_div_row, sizeof(body_fostered_div_row) - 1,
                       "table row after fostered body div");
    if (status == 0)
        status = sweep(body_fostered_section_span,
                       sizeof(body_fostered_section_span) - 1,
                       "table parts after fostered section and span");
    if (status == 0)
        status = sweep(body_fostered_blocks, sizeof(body_fostered_blocks) - 1,
                       "table parts after fostered block containers");
    if (status == 0)
        status = sweep(body_table_select_textarea,
                       sizeof(body_table_select_textarea) - 1,
                       "table select textarea reprocess");
    if (status == 0)
        status = sweep(body_table_text_modes,
                       sizeof(body_table_text_modes) - 1,
                       "fostered table text modes");
    if (status == 0)
        status = sweep(select_closed_by_input,
                       sizeof(select_closed_by_input) - 1,
                       "select closed by table input");
    if (status == 0)
        status = sweep(select_closed_by_keygen,
                       sizeof(select_closed_by_keygen) - 1,
                       "select closed by keygen");
    if (status == 0)
        status = sweep(orphan_option_body, sizeof(orphan_option_body) - 1,
                       "orphan options after closed select");
    if (status == 0)
        status = sweep(fostered_table_start,
                       sizeof(fostered_table_start) - 1,
                       "table starts after fostered descendants");
    if (status == 0)
        status = sweep(table_plaintext, sizeof(table_plaintext) - 1,
                       "template table plaintext");
    if (status == 0)
        status = sweep(unclosed_foster, sizeof(unclosed_foster) - 1,
                       "unclosed template fostered items");
    if (status == 0)
        status = sweep(direct_row, sizeof(direct_row) - 1,
                       "direct template row");
    if (status == 0)
        status = sweep(direct_section, sizeof(direct_section) - 1,
                       "direct template section");
    if (status == 0)
        status = sweep(direct_columns, sizeof(direct_columns) - 1,
                       "direct template columns");
    if (status == 0)
        status = sweep(direct_group_content,
                       sizeof(direct_group_content) - 1,
                       "direct template column group content");
    if (status == 0)
        status = sweep(column_after_fostered,
                       sizeof(column_after_fostered) - 1,
                       "direct columns after fostered descendants");
    if (status == 0)
        status = sweep(direct_part_after_fostered,
                       sizeof(direct_part_after_fostered) - 1,
                       "direct parts after fostered descendants");
    if (status == 0)
        status = sweep(direct_table_start,
                       sizeof(direct_table_start) - 1,
                       "table starts after fostered direct content");
    if (status == 0)
        status = sweep(integration_objects,
                       sizeof(integration_objects) - 1,
                       "objects in template foreign HTML integration");
    if (status == 0)
        status = sweep(nested_math_text, sizeof(nested_math_text) - 1,
                       "foreign descendants of MathML text integration");
    if (status == 0)
        status = sweep(cross_foreign_roots,
                       sizeof(cross_foreign_roots) - 1,
                       "SVG and MathML roots inside foreign content");
    if (status == 0)
        status = sweep(void_body_mode, sizeof(void_body_mode) - 1,
                       "void template body mode");
    if (status == 0)
        status = sweep(text_body_mode, sizeof(text_body_mode) - 1,
                       "text template body mode");
    if (status == 0)
        status = sweep(crossed_menu, sizeof(crossed_menu) - 1,
                       "crossed template menu");
    if (status == 0)
        status = sweep(crossed_list, sizeof(crossed_list) - 1,
                       "crossed template list");
    if (status == 0)
        status = sweep(crossed_open_list_item,
                       sizeof(crossed_open_list_item) - 1,
                       "crossed open template item");
    if (status == 0)
        status = sweep(crossed_multi_list_item,
                       sizeof(crossed_multi_list_item) - 1,
                       "crossed multiple template items");
    if (status == 0)
        status = sweep(crossed_formatted_list_item,
                       sizeof(crossed_formatted_list_item) - 1,
                       "crossed formatted template item");
    if (status == 0)
        status = sweep(crossed_nested_list_item,
                       sizeof(crossed_nested_list_item) - 1,
                       "crossed nested template item");
    if (status == 0)
        status = sweep(crossed_span_list_item,
                       sizeof(crossed_span_list_item) - 1,
                       "crossed span template item");
    if (status == 0)
        status = sweep(crossed_nested_spans,
                       sizeof(crossed_nested_spans) - 1,
                       "crossed nested spans");
    if (status == 0)
        status = sweep(crossed_div_list_item,
                       sizeof(crossed_div_list_item) - 1,
                       "crossed direct div");
    if (status == 0)
        status = sweep(crossed_paragraph_list_item,
                       sizeof(crossed_paragraph_list_item) - 1,
                       "crossed direct paragraph");
    if (status == 0)
        status = sweep(crossed_div_span, sizeof(crossed_div_span) - 1,
                       "crossed div span");
    if (status == 0)
        status = sweep(crossed_div_formatting,
                       sizeof(crossed_div_formatting) - 1,
                       "crossed div formatting");
    if (status == 0)
        status = sweep(crossed_span_div, sizeof(crossed_span_div) - 1,
                       "crossed span div");
    if (status == 0)
        status = sweep(crossed_inline_containers,
                       sizeof(crossed_inline_containers) - 1,
                       "crossed inline containers in template lists");
    if (status == 0)
        status = sweep(crossed_quote_containers,
                       sizeof(crossed_quote_containers) - 1,
                       "crossed quote containers in template lists");
    if (status == 0)
        status = sweep(crossed_nested_block,
                       sizeof(crossed_nested_block) - 1,
                       "crossed nested block");
    if (status == 0)
        status = sweep(crossed_two_blocks,
                       sizeof(crossed_two_blocks) - 1,
                       "crossed template list two-block combinations");
    if (status == 0)
        status = sweep(crossed_three_blocks,
                       sizeof(crossed_three_blocks) - 1,
                       "crossed template list three-block chain");
    if (status == 0)
        status = sweep(crossed_deep_blocks,
                       sizeof(crossed_deep_blocks) - 1,
                       "crossed template list five-block chain");
    if (status == 0)
        status = sweep(crossed_nested_inline,
                       sizeof(crossed_nested_inline) - 1,
                       "crossed template nested inline chain");
    if (status == 0)
        status = sweep(crossed_surrounding_inline,
                       sizeof(crossed_surrounding_inline) - 1,
                       "crossed template surrounding inline chain");
    if (status == 0)
        status = sweep(crossed_nested_formatting,
                       sizeof(crossed_nested_formatting) - 1,
                       "crossed template nested formatting child");
    if (status == 0)
        status = sweep(crossed_interposed_inline,
                       sizeof(crossed_interposed_inline) - 1,
                       "crossed template interposed inline chain");
    if (status == 0)
        status = sweep(crossed_both_inline,
                       sizeof(crossed_both_inline) - 1,
                       "crossed template inline chains on both sides");
    if (status == 0)
        status = sweep(crossed_nested_gap,
                       sizeof(crossed_nested_gap) - 1,
                       "crossed template nested block inline gap");
    if (status == 0)
        status = sweep(crossed_two_gaps,
                       sizeof(crossed_two_gaps) - 1,
                       "crossed template two inline gaps");
    if (status == 0)
        status = sweep(colgroup_formatting,
                       sizeof(colgroup_formatting) - 1,
                       "colgroup formatting");
    if (status == 0)
        status = sweep(colgroup_foreign,
                       sizeof(colgroup_foreign) - 1,
                       "colgroup fostered foreign content");
    if (status == 0)
        status = sweep(table_foreign, sizeof(table_foreign) - 1,
                       "template table-section fostered foreign content");
    if (status == 0)
        status = sweep(table_inline, sizeof(table_inline) - 1,
                       "template table-section fostered inline content");
    if (status == 0)
        status = sweep(table_dialogs, sizeof(table_dialogs) - 1,
                       "template table-section fostered dialogs");
    if (status == 0)
        status = sweep(template_details, sizeof(template_details) - 1,
                       "template details and fostered summary");
    if (status == 0)
        status = sweep(template_fieldsets,
                       sizeof(template_fieldsets) - 1,
                       "template fieldsets and fostered legend");
    if (status == 0)
        status = sweep(template_figcaptions,
                       sizeof(template_figcaptions) - 1,
                       "template figure captions in table contexts");
    if (status == 0)
        status = sweep(template_more_blocks,
                       sizeof(template_more_blocks) - 1,
                       "template fostered hgroup search and dir");
    if (status == 0)
        status = sweep(template_noscript, sizeof(template_noscript) - 1,
                       "template table noscript fostering");
    if (status == 0)
        status = sweep(template_media, sizeof(template_media) - 1,
                       "template table media fostering");
    if (status == 0)
        status = sweep(template_phrasing, sizeof(template_phrasing) - 1,
                       "template phrasing and table fostering");
    if (status == 0)
        status = sweep(template_values, sizeof(template_values) - 1,
                       "template value elements and table fostering");
    if (status == 0)
        status = sweep(crossed_long_gaps, sizeof(crossed_long_gaps) - 1,
                       "crossed template blocks with inline gaps");
    if (status == 0)
        status = sweep(template_maps, sizeof(template_maps) - 1,
                       "template map and area fostering");
    if (status == 0)
        status = sweep(template_datalists, sizeof(template_datalists) - 1,
                       "template datalist option fostering");
    if (status == 0)
        status = sweep(svg_descendants, sizeof(svg_descendants) - 1,
                       "template SVG foreign descendants");
    if (status == 0)
        status = sweep(math_breakouts, sizeof(math_breakouts) - 1,
                       "template MathML foreign starts and HTML breakouts");
    if (status == 0)
        status = sweep(math_nested_foreign,
                       sizeof(math_nested_foreign) - 1,
                       "template nested MathML foreign descendants");
    if (status == 0)
        status = sweep(svg_nested_breakouts,
                       sizeof(svg_nested_breakouts) - 1,
                       "template nested SVG HTML breakout starts");
    if (status == 0)
        status = sweep(svg_integrated_containers,
                       sizeof(svg_integrated_containers) - 1,
                       "template SVG HTML integration containers");
    if (status == 0)
        status = sweep(svg_integrated_text,
                       sizeof(svg_integrated_text) - 1,
                       "template SVG HTML integration text modes");
    if (status == 0)
        status = sweep(math_integrated_content,
                       sizeof(math_integrated_content) - 1,
                       "template MathML HTML integration content");
    if (status == 0)
        status = sweep(svg_math_breakouts,
                       sizeof(svg_math_breakouts) - 1,
                       "template SVG breakouts through MathML");
    if (status == 0)
        status = sweep(template_transparent,
                       sizeof(template_transparent) - 1,
                       "template and table transparent containers");
    if (status == 0)
        status = sweep(template_custom_foster,
                       sizeof(template_custom_foster) - 1,
                       "template table custom element fostering");
    if (status == 0)
        status = sweep(crossed_foster_order,
                       sizeof(crossed_foster_order) - 1,
                       "crossed fostered list sibling ordering");
    if (status == 0)
        status = sweep(crossed_list_trailing_inline,
                       sizeof(crossed_list_trailing_inline) - 1,
                       "crossed list formatting with trailing inline");
    if (status == 0)
        status = sweep(crossed_list_interposed_formatting,
                       sizeof(crossed_list_interposed_formatting) - 1,
                       "crossed list formatting across inline children");
    if (status == 0)
        status = sweep(crossed_list_formatted_block,
                       sizeof(crossed_list_formatted_block) - 1,
                       "crossed list block under active formatting");
    if (status == 0)
        status = sweep(crossed_list_formatting_chain,
                       sizeof(crossed_list_formatting_chain) - 1,
                       "crossed list block under three formatting entries");
    if (status == 0)
        status = sweep(crossed_list_long_formatting_chain,
                       sizeof(crossed_list_long_formatting_chain) - 1,
                       "crossed list block beyond adoption inner limit");
    if (status == 0)
        status = sweep(crossed_list_preblock_inline,
                       sizeof(crossed_list_preblock_inline) - 1,
                       "crossed list block after inline gap");
    if (status == 0)
        status = sweep(crossed_list_interposed_inline,
                       sizeof(crossed_list_interposed_inline) - 1,
                       "crossed list block after interposed inline");
    if (status == 0)
        status = sweep(crossed_list_closure,
                       sizeof(crossed_list_closure) - 1,
                       "single formatting continuation after list closure");
    if (status == 0)
        status = sweep(following_list_block,
                       sizeof(following_list_block) - 1,
                       "formatting reconstructed in following list blocks");
    if (status == 0)
        status = sweep(closed_list_block,
                       sizeof(closed_list_block) - 1,
                       "formatting retained after closing following blocks");
    if (status == 0)
        status = sweep(following_list_section,
                       sizeof(following_list_section) - 1,
                       "formatting retained after following section blocks");
    if (status == 0)
        status = sweep(following_list_heading,
                       sizeof(following_list_heading) - 1,
                       "formatting retained after following headings");
    if (status == 0)
        status = sweep(following_list_void,
                       sizeof(following_list_void) - 1,
                       "formatting reconstructed before following voids");
    if (status == 0)
        status = sweep(following_list_void_chain,
                       sizeof(following_list_void_chain) - 1,
                       "following void chain under reconstructed formatting");
    if (status == 0)
        status = sweep(following_list_rule,
                       sizeof(following_list_rule) - 1,
                       "following horizontal rule outside formatting");
    if (status == 0)
        status = sweep(following_list_item,
                       sizeof(following_list_item) - 1,
                       "formatting reconstructed in following list item");
    if (status == 0)
        status = sweep(following_list_siblings,
                       sizeof(following_list_siblings) - 1,
                       "formatting retained across following list items");
    if (status == 0)
        status = sweep(nested_list_continuation,
                       sizeof(nested_list_continuation) - 1,
                       "formatting retained across a div and nested list");
    if (status == 0)
        status = sweep(following_list_spans,
                       sizeof(following_list_spans) - 1,
                       "formatting reconstructed around following spans");
    if (status == 0)
        status = sweep(crossed_following_span,
                       sizeof(crossed_following_span) - 1,
                       "crossed formatting end under following span");
    if (status == 0)
        status = sweep(following_span_voids,
                       sizeof(following_span_voids) - 1,
                       "following voids under reconstructed span");
    if (status == 0)
        status = sweep(rule_after_formatting,
                       sizeof(rule_after_formatting) - 1,
                       "rule inside reconstructed formatting");
    if (status == 0)
        status = sweep(blocks_inside_formatting,
                       sizeof(blocks_inside_formatting) - 1,
                       "following blocks inside reconstructed formatting");
    if (status == 0)
        status = sweep(nested_formatting_spans,
                       sizeof(nested_formatting_spans) - 1,
                       "nested spans under reconstructed formatting");
    if (status == 0)
        status = sweep(nested_span_voids,
                       sizeof(nested_span_voids) - 1,
                       "voids under nested reconstructed spans");
    if (status == 0)
        status = sweep(blocks_in_nested_spans,
                       sizeof(blocks_in_nested_spans) - 1,
                       "blocks under nested reconstructed spans");
    if (status == 0)
        status = sweep(deep_list_continuation,
                       sizeof(deep_list_continuation) - 1,
                       "following list through nested div blocks");
    if (status == 0)
        status = sweep(following_special_blocks,
                       sizeof(following_special_blocks) - 1,
                       "special blocks after closed list formatting");
    if (status == 0)
        status = sweep(objects_after_lists,
                       sizeof(objects_after_lists) - 1,
                       "objects under reconstructed formatting");
    if (status == 0)
        status = sweep(nested_following_lists,
                       sizeof(nested_following_lists) - 1,
                       "nested following lists before reconstruction");
    if (status == 0)
        status = sweep(formatted_nested_list,
                       sizeof(formatted_nested_list) - 1,
                       "nested list under reconstructed list formatting");
    if (status == 0)
        status = sweep(list_div_descendants,
                       sizeof(list_div_descendants) - 1,
                       "nested list under direct div descendants");
    if (status == 0)
        status = sweep(formatted_list_divs,
                       sizeof(formatted_list_divs) - 1,
                       "nested list below reconstructed formatting and divs");
    if (status == 0)
        status = sweep(formatted_list_blocks,
                       sizeof(formatted_list_blocks) - 1,
                       "nested list below reconstructed section blocks");
    if (status == 0)
        status = sweep(markers_after_lists,
                       sizeof(markers_after_lists) - 1,
                       "markers under reconstructed formatting");
    if (status == 0)
        status = sweep(text_modes_after_list,
                       sizeof(text_modes_after_list) - 1,
                       "text modes after retained template formatting");
    if (status == 0)
        status = sweep(raw_modes_after_list,
                       sizeof(raw_modes_after_list) - 1,
                       "raw text modes after retained template formatting");
    if (status == 0)
        status = sweep(plaintext_after_list,
                       sizeof(plaintext_after_list) - 1,
                       "plaintext after retained template formatting");
    if (status == 0)
        status = sweep(following_preformatted,
                       sizeof(following_preformatted) - 1,
                       "preformatted blocks after retained formatting");
    if (status == 0)
        status = sweep(foreign_after_list,
                       sizeof(foreign_after_list) - 1,
                       "foreign roots under reconstructed formatting");
    if (status == 0)
        status = sweep(foreign_under_formatting,
                       sizeof(foreign_under_formatting) - 1,
                       "foreign roots inside open template formatting");
    if (status == 0)
        status = sweep(foreign_descendants_after_list,
                       sizeof(foreign_descendants_after_list) - 1,
                       "foreign descendants after template list closure");
    if (status == 0)
        status = sweep(multiple_list_formatting,
                       sizeof(multiple_list_formatting) - 1,
                       "multiple formatting entries after list closure");
    if (status == 0)
        status = sweep(multiple_formatting_spans,
                       sizeof(multiple_formatting_spans) - 1,
                       "multiple formatting entries under following spans");
    if (status == 0)
        status = sweep(multiple_formatting_div,
                       sizeof(multiple_formatting_div) - 1,
                       "multiple formatting entries under following blocks");
    if (status == 0)
        status = sweep(multiple_formatting_ends,
                       sizeof(multiple_formatting_ends) - 1,
                       "closing multiple pending formatting entries");
    if (status == 0)
        status = sweep(multiple_formatting_middle,
                       sizeof(multiple_formatting_middle) - 1,
                       "closing interposed template formatting entries");
    if (status == 0)
        status = sweep(multiple_formatting_span_end,
                       sizeof(multiple_formatting_span_end) - 1,
                       "closing formatting across a following span");
    if (status == 0)
        status = sweep(multiple_formatting_block_end,
                       sizeof(multiple_formatting_block_end) - 1,
                       "closing formatting inside a following block");
    if (status == 0)
        status = sweep(multiple_formatting_block_span_end,
                       sizeof(multiple_formatting_block_span_end) - 1,
                       "closing formatting inside a block and span");
    if (status == 0)
        status = sweep(multiple_formatting_nested_block_end,
                       sizeof(multiple_formatting_nested_block_end) - 1,
                       "closing formatting inside nested blocks");
    if (status == 0)
        status = sweep(multiple_formatting_article,
                       sizeof(multiple_formatting_article) - 1,
                       "following article and implicit paragraph closure");
    if (status == 0)
        status = sweep(multiple_formatting_direct_blocks,
                       sizeof(multiple_formatting_direct_blocks) - 1,
                       "following direct blocks and preformatted text");
    if (status == 0)
        status = sweep(multiple_formatting_headings,
                       sizeof(multiple_formatting_headings) - 1,
                       "following headings and mismatched heading end");
    if (status == 0)
        status = sweep(multiple_formatting_following_list,
                       sizeof(multiple_formatting_following_list) - 1,
                       "following list under multiple formatting entries");
    if (status == 0)
        status = sweep(multiple_formatting_nested_list,
                       sizeof(multiple_formatting_nested_list) - 1,
                       "nested following list under multiple formatting");
    if (status == 0)
        status = sweep(multiple_formatting_outer_list_end,
                       sizeof(multiple_formatting_outer_list_end) - 1,
                       "outer list end across open nested item");
    if (status == 0)
        status = sweep(nested_formatted_item_ends,
                       sizeof(nested_formatted_item_ends) - 1,
                       "closing formatted items after nested lists");
    if (status == 0)
        status = sweep(nested_crossed_formatting,
                       sizeof(nested_crossed_formatting) - 1,
                       "crossed formatting end after nested list");
    if (status == 0)
        status = sweep(multiple_formatting_foreign,
                       sizeof(multiple_formatting_foreign) - 1,
                       "foreign roots under multiple pending entries");
    if (status == 0)
        status = sweep(multiple_formatting_paragraphs,
                       sizeof(multiple_formatting_paragraphs) - 1,
                       "paragraphs under multiple pending entries");
    if (status == 0)
        status = sweep(crossed_direct_blocks,
                       sizeof(crossed_direct_blocks) - 1,
                       "direct crossed template blocks");
    if (status == 0)
        status = sweep(crossed_sectioning,
                       sizeof(crossed_sectioning) - 1,
                       "direct crossed template sectioning");
    if (status == 0)
        status = sweep(crossed_special_blocks,
                       sizeof(crossed_special_blocks) - 1,
                       "direct crossed special blocks");
    if (status == 0)
        status = sweep(crossed_block_inline,
                       sizeof(crossed_block_inline) - 1,
                       "crossed block with inline child");
    if (status == 0)
        status = sweep(crossed_block_formatting,
                       sizeof(crossed_block_formatting) - 1,
                       "crossed block formatting reconstruction");
    if (status == 0)
        status = sweep(crossed_block_formatting_pair,
                       sizeof(crossed_block_formatting_pair) - 1,
                       "crossed block nested formatting reconstruction");
    if (status == 0)
        status = sweep(crossed_block_three_formatting,
                       sizeof(crossed_block_three_formatting) - 1,
                       "crossed block three formatting descendants");
    if (status == 0)
        status = sweep(crossed_block_inline_gap,
                       sizeof(crossed_block_inline_gap) - 1,
                       "crossed block formatting across inline gaps");
    if (status == 0)
        status = sweep(crossed_formatting_trailing_inline,
                       sizeof(crossed_formatting_trailing_inline) - 1,
                       "crossed formatting with trailing inline containers");
    if (status == 0)
        status = sweep(crossed_interposed_formatting,
                       sizeof(crossed_interposed_formatting) - 1,
                       "crossed formatting across inline children");
    if (status == 0)
        status = sweep(integrated_templates,
                       sizeof(integrated_templates) - 1,
                       "nested templates at foreign integration points");
    if (status == 0)
        status = sweep(nested_objects, sizeof(nested_objects) - 1,
                       "template nested object and select recovery");
    if (status == 0)
        status = sweep(template_buttons,
                       sizeof(template_buttons) - 1,
                       "template table and body button recovery");
    if (status == 0)
        status = sweep(table_orphan_options,
                       sizeof(table_orphan_options) - 1,
                       "template table orphan option recovery");
    if (status == 0)
        status = sweep(svg_buttons, sizeof(svg_buttons) - 1,
                       "template SVG button integration");
    if (status == 0)
        status = sweep(math_buttons, sizeof(math_buttons) - 1,
                       "template MathML button integration");
    if (status == 0)
        status = sweep(svg_integration_blocks,
                       sizeof(svg_integration_blocks) - 1,
                       "template SVG integrated blocks and lists");
    if (status == 0)
        status = sweep(math_integration_lists,
                       sizeof(math_integration_lists) - 1,
                       "template MathML integrated and breakout lists");
    if (status == 0)
        status = sweep(foreign_articles,
                       sizeof(foreign_articles) - 1,
                       "template foreign article namespaces");
    if (status == 0)
        status = sweep(foreign_sectioning,
                       sizeof(foreign_sectioning) - 1,
                       "template foreign sectioning namespaces");
    if (status == 0)
        status = sweep(foreign_simple_inline,
                       sizeof(foreign_simple_inline) - 1,
                       "template foreign simple inline namespaces");
    if (status == 0)
        status = sweep(colgroup_fostered_heading,
                       sizeof(colgroup_fostered_heading) - 1,
                       "colgroup fostered heading and menu");
    if (status == 0)
        status = sweep(colgroup_fostered_pre,
                       sizeof(colgroup_fostered_pre) - 1,
                       "colgroup fostered pre and listing");
    if (status == 0)
        status = sweep(colgroup_text_modes, sizeof(colgroup_text_modes) - 1,
                       "colgroup style and script text modes");
    if (status == 0)
        status = sweep(column_after_descendants,
                       sizeof(column_after_descendants) - 1,
                       "columns after cell and caption descendants");
    if (status == 0)
        status = sweep(column_after_math, sizeof(column_after_math) - 1,
                       "columns after fostered MathML integration text");
    if (status == 0)
        status = sweep(foreign_table_cells,
                       sizeof(foreign_table_cells) - 1,
                       "foreign roots in template table cells and captions");
    if (status == 0)
        status = sweep(math_table_parts, sizeof(math_table_parts) - 1,
                       "table parts after MathML text integration");
    if (status == 0)
        status = sweep(direct_section_math,
                       sizeof(direct_section_math) - 1,
                       "direct table sections after MathML text integration");
    if (status == 0)
        status = sweep(direct_row_math, sizeof(direct_row_math) - 1,
                       "direct rows after MathML text integration");
    if (status == 0)
        status = sweep(direct_row_cells, sizeof(direct_row_cells) - 1,
                       "adjacent cells and rows in direct template rows");
    if (status == 0)
        status = sweep(direct_row_cell_descendants,
                       sizeof(direct_row_cell_descendants) - 1,
                       "direct row cells with open descendants");
    if (status == 0)
        status = sweep(direct_row_math_parts,
                       sizeof(direct_row_math_parts) - 1,
                       "ignored table parts after direct row MathML");
    if (status == 0)
        status = sweep(direct_section_annotation,
                       sizeof(direct_section_annotation) - 1,
                       "direct table sections after MathML HTML integration");
    if (status == 0)
        status = sweep(template_cell_forms, sizeof(template_cell_forms) - 1,
                       "template forms inside table cells and captions");
    if (status == 0)
        status = sweep(template_cell_objects,
                       sizeof(template_cell_objects) - 1,
                       "template objects inside table cells and captions");
    if (status == 0)
        status = sweep(fostered_objects, sizeof(fostered_objects) - 1,
                       "objects inside fostered template descendants");
    if (status == 0)
        status = sweep(fostered_marquee_applet,
                       sizeof(fostered_marquee_applet) - 1,
                       "marquee and applet in fostered template rows");
    if (status == 0)
        status = sweep(direct_row_ignored_parts,
                       sizeof(direct_row_ignored_parts) - 1,
                       "ignored table parts after open direct row descendants");
    if (status == 0)
        status = sweep(direct_row_columns,
                       sizeof(direct_row_columns) - 1,
                       "columns without a direct row table");
    if (status == 0)
        status = sweep(direct_table_ends, sizeof(direct_table_ends) - 1,
                       "table ends in direct rows and sections");
    if (status == 0)
        status = sweep(direct_section_mismatched_ends,
                       sizeof(direct_section_mismatched_ends) - 1,
                       "mismatched section ends above direct rows");
    if (status == 0)
        status = sweep(table_mismatched_ends,
                       sizeof(table_mismatched_ends) - 1,
                       "mismatched section ends in template tables");
    if (status == 0)
        status = sweep(nested_table_mismatched_ends,
                       sizeof(nested_table_mismatched_ends) - 1,
                       "mismatched section ends in nested template tables");
    if (status == 0)
        status = sweep(table_noframes, sizeof(table_noframes) - 1,
                       "noframes raw text in template table modes");
    if (status == 0)
        status = sweep(keygen_void, sizeof(keygen_void) - 1,
                       "keygen void elements in body and template tables");
    if (status == 0)
        status = sweep(isindex_container, sizeof(isindex_container) - 1,
                       "isindex containers in body and template tables");
    if (status == 0)
        status = sweep(legacy_void_head_table,
                       sizeof(legacy_void_head_table) - 1,
                       "basefont and bgsound in head and tables");
    if (status == 0)
        status = sweep(closed_head_noscript,
                       sizeof(closed_head_noscript) - 1,
                       "head noscript recovery after nonspace text");
    if (status == 0)
        status = sweep(legacy_containers,
                       sizeof(legacy_containers) - 1,
                       "legacy ordinary containers in body and tables");
    if (status == 0)
        status = sweep(fostered_table_metadata,
                       sizeof(fostered_table_metadata) - 1,
                       "fostered template table metadata");
    if (status == 0)
        status = sweep(fostered_table_titles,
                       sizeof(fostered_table_titles) - 1,
                       "fostered template table title text modes");
    if (status == 0)
        status = sweep(paragraph_table, sizeof(paragraph_table) - 1,
                       "table and fostered text inside template paragraph");
    if (status == 0)
        status = sweep(colgroup_form, sizeof(colgroup_form) - 1,
                       "ignored template colgroup form");
    if (status == 0)
        status = sweep(body_descendant, sizeof(body_descendant) - 1,
                       "template body descendant");
    if (status == 0)
        status = sweep(direct_row_foster, sizeof(direct_row_foster) - 1,
                       "direct template row fostering");
    if (status == 0)
        status = sweep(direct_row_text, sizeof(direct_row_text) - 1,
                       "direct template row text");
    return status != 0 ? status
           : sweep(fostered_row_cell, sizeof(fostered_row_cell) - 1,
                   "fostered direct row cell");
}
