#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "openwrite_core.h"
#include "ow_spell.h"

int main(void)
{
    FILE *f;
    ow_spell *sp;
    owf_doc *doc;
    owf_para *p;
    owf_parafmt pf;
    owf_charfmt cf;
    ow_editor *ed;
    char status[128], word[64], sug[4][48];

    f=fopen("/tmp/openwrite-spell.words","w");
    if(!f)return 1;
    fputs("hello\nopenwrite\nworld\nword\n",f);
    fclose(f);
    sp=ow_spell_open("/tmp/openwrite-spell.words","/tmp/openwrite-user.words",status,sizeof status);
    if(!sp)return 2;
    doc=owf_doc_new();if(!doc)return 3;
    owf_parafmt_init(&pf);owf_charfmt_init(&cf);
    p=owf_story_add(&doc->body,&pf);
    if(!p||owf_para_add_text(p,&cf,"hello wrold",11)!=OWF_OK)return 4;
    ed=ow_editor_new(doc);if(!ed)return 5;
    if(ow_spell_next(sp,ed,word,sizeof word,1)!=1||strcmp(word,"wrold"))return 6;
    if(ow_spell_suggest(sp,word,sug,4)<1)return 7;
    if(ow_editor_insert_utf8(ed,"world",5)!=OWF_OK)return 8;
    if(strcmp(doc->body.paras[0].runs[0].text,"hello world"))return 9;
    if(ow_spell_next(sp,ed,word,sizeof word,1)!=0)return 10;
    if(!ow_spell_add(sp,"dalsin"))return 11;
    ow_editor_free(ed);owf_doc_free(doc);ow_spell_close(sp);
    remove("/tmp/openwrite-spell.words");remove("/tmp/openwrite-user.words");
    puts("openwrite spell tests passed");
    return 0;
}
