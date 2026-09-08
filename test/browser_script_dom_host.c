/* Included by the real parser host; no alternate parser or DOM model. */
static unsigned script_calls;
static unsigned external_calls;
static char script_snapshot[2U*1024U*1024U];
static node *script_target,*script_root;
static const char *script_attribute(node *n,const char *name) {
    for(attribute *a=n->attributes;a;a=a->next) if(!strcmp(a->name,name)) return a->value;
    return NULL;
}
static node *script_by_id(node *n,const char *id) {
    if(n->kind==1) for(attribute *a=n->attributes;a;a=a->next)
        if(!strcmp(a->name,"id") && !strcmp(a->value,id)) return n;
    for(node *c=n->first;c;c=c->next) { node *found=script_by_id(c,id); if(found) return found; }
    return NULL;
}
static int script_test_hook(void *unused,node *n) {
    (void)unused; uint32_t snapshot=0,source=0;
    script_root=n; while(script_root->parent) script_root=script_root->parent;
    assert(!browser_html_script_snapshot(n,"https://example.test/a?x=\"\\",script_snapshot,sizeof(script_snapshot),&snapshot,&source));
    assert(snapshot && source==5 && !memcmp(script_snapshot,"__reistDOM.sync(\"",17));
    assert(!memcmp(script_snapshot+snapshot,script_calls ? "two()" : "one()",5));
    assert(!script_by_id(script_root,"future"));
    script_target=script_by_id(script_root,"target"); assert(script_target);
    if(!script_calls) {
        uint32_t id=(uint32_t)(script_target-script_root)+1;
        char valid[128],bad[256];
        snprintf(valid,sizeof(valid),"%08x000000043c263e4100000000000000034a5321",id);
        snprintf(bad,sizeof(bad),"%s000020000000000178",valid);
        node *before=script_target->first;
        assert(browser_html_script_apply(bad,(uint32_t)strlen(bad))<0);
        assert(script_target->first==before && !strcmp(before->text,"old"));
        assert(browser_html_script_apply("0000000000000002c080",20)<0); /* overlong UTF-8 */
        assert(browser_html_script_apply("0000000000000003000000",22)<0); /* NUL */
        assert(!browser_html_script_apply(valid,(uint32_t)strlen(valid)));
        assert(script_target->first!=before && !before->parent);
        assert(!strcmp(script_target->first->text,"<&>A") && !script_target->first->next);
    } else assert(!strcmp(script_target->first->text,"<&>A"));
    if(!script_calls) {
        uint32_t id=(uint32_t)(script_target-script_root)+1;
        char valid[256],bad[512];
        snprintf(valid,sizeof(valid),"00000001%08x0000000500000004636c617373676f6f64"
            "00000001%08x0000000200000005637376616c7565",id,id); /* class=good, cs=value */
        snprintf(bad,sizeof(bad),"%s00000003000000010000000000000000",valid);
        assert(browser_html_script_apply_version(bad,(uint32_t)strlen(bad),2)<0);
        assert(!script_attribute(script_target,"class"));
        assert(!browser_html_script_apply_version(valid,(uint32_t)strlen(valid),2));
        assert(!strcmp(script_attribute(script_target,"class"),"good") && !strcmp(script_attribute(script_target,"cs"),"value"));
        snprintf(valid,sizeof(valid),"00000002%08x00000002000000006373",id);
        assert(!browser_html_script_apply_version(valid,(uint32_t)strlen(valid),2));
        assert(!script_attribute(script_target,"cs"));
        assert(!browser_html_script_snapshot_version(n,"https://example.test/",script_snapshot,sizeof(script_snapshot),&snapshot,&source,2));
        script_snapshot[snapshot]=0;
        assert(find(script_snapshot,"[\"id\",\"target\"],[\"class\",\"good\"]"));
        snprintf(valid,sizeof(valid),"00000001%08x00000002000000056f4e76616c7565",id); /* uppercase wire name */
        assert(browser_html_script_apply_version(valid,(uint32_t)strlen(valid),2)<0);
        assert(!script_attribute(script_target,"oN"));
    } else assert(!strcmp(script_attribute(script_target,"class"),"good"));
    ++script_calls; return 0;
}
static int external_test_hook(void *unused,node *n) {
    (void)unused; uint32_t snapshot=0,source=0;
    assert(!browser_html_script_snapshot_version(n,"https://example.test/page",script_snapshot,sizeof(script_snapshot),&snapshot,&source,3));
    const char *ref=script_snapshot+snapshot;
    assert(!strcmp(ref,"/assets/") && source==9+strlen("relative.js"));
    assert(!memcmp(ref+9,"relative.js",strlen("relative.js")));
    node *root=n;while(root->parent)root=root->parent;
    assert(!script_by_id(root,"future"));
    assert(!memcmp(script_snapshot+snapshot-5,"],2);",5));
    ++external_calls;return 0;
}
static void script_dom_test(void) {
    const char html[]="<!doctype html><title>old</title><body><p id=target>old</p>"
        "<script>one()</script><script src=''>inert</script><script type=module>inert</script>"
        "<script type=application/json>inert</script><template><script>inert</script></template>"
        "<svg><script>inert</script></svg><noscript><script>inert</script></noscript>"
        "<script>two()</script><p id=future>future</p>";
    node *root=NULL; browser_html_script_hook_set(script_test_hook,NULL);
    assert(!browser_html5_document_tree((const uint8_t *)html,sizeof(html)-1,BROWSER_ENCODING_UTF8,&root));
    assert(script_calls==2 && script_by_id(root,"future"));
    browser_html5_document_release();
    assert(!reist_libc_reset()); legacy_used=0;
    const char csp[]="<meta http-equiv=Content-Security-Policy content=default-src><script>inert</script>";
    assert(!browser_html5_document_tree((const uint8_t *)csp,sizeof(csp)-1,BROWSER_ENCODING_UTF8,&root));
    assert(script_calls==2); browser_html5_document_release();
    assert(!reist_libc_reset()); legacy_used=0;
    browser_html_script_external_enable(1); browser_html_script_hook_set(external_test_hook,NULL);
    const char ext[]="<base><base href='/assets/'><base href='/ignored/'><body>"
        "<script src='relative.js' type='TEXT/JAVASCRIPT1.5'>not fallback</script>"
        "<script src=x async></script><script src=x defer></script><script src=x type=module></script>"
        "<script src=x crossorigin></script><script src=x integrity=''></script><script src=x referrerpolicy=''></script>"
        "<script src=x type='text/javascript;charset=utf-8'></script><script src=''></script>"
        "<template><script src=x></script></template><svg><script src=x></script></svg><p id=future>future";
    assert(!browser_html5_document_tree((const uint8_t *)ext,sizeof(ext)-1,BROWSER_ENCODING_UTF8,&root));
    assert(external_calls==1); browser_html5_document_release();
    browser_html_script_external_enable(0); browser_html_script_hook_set(NULL,NULL);
    assert(!reist_libc_reset()); legacy_used=0;
    browser_html_script_hook_set(NULL,NULL);
    assert(!browser_html5_document_tree((const uint8_t *)html,sizeof(html)-1,BROWSER_ENCODING_UTF8,&root));
    assert(script_calls==2); browser_html5_document_release();
    puts("HTML5_SCRIPT_BOUNDARY_JOURNAL_OK");
}
