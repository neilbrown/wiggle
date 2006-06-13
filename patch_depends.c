
/*
 * Given a list of files containing patches, we determine any dependancy
 * relationship between them.
 * If a chunk in one file overlaps a chunk in a previous file then the one
 * depends on the other.
 *
 * Each patch contains a list of chunks that apply to a file. Each
 * chunk has an original start/end and a new start/end.
 *
 * Each target file links to  a list of chunks, each of which points to it's
 * patch file. The chunks are sorted by new start
 *
 * When we add a chunk which changes size, we update the new start/end of all
 * previous chunks in that file which end after this one starts.
 *
 */



struct chunk {
	struct patch *patch;	/* the patch this chunk is from */
	struct file *file;	/* the file this chunk patches */
	int old_start, old_end;
	int new_start, new_end;
	struct chunk *next;	/* next chunk for this file */
};

struct file {
	char * name;		/* name of the file */
	struct chunk *chunks;	/* chunks which patch this file */
};

struct patch {
	char * name;		/* name of file containing this patch */
	int cnt;		/* number of patches we depend on (so far) */
	struct patch *depends;	/* array of patches we depend on */
	struct patch *next;	/* previous patch that was loaded */
} *patches = NULL;



void report(void)
{
	struct patch *p;
	int c;

	for (p= patches; p ; p=p->next) {
		printf("%s :", p->name);
		for (c=0 ; c < p->cnt ; c++)
			printf(" %s", p->depends[c]);
		printf("\n");
	}
}

int check_depends(struct patch *new, struct patch *old)
{
	/* see if new already depends on old */
	int i;
	if (new == old) return 1;
	for (i=0; i<new->cnt ; i++)
		if (check_depends(new->depends[i], old))
			return 1;
	return 0;
}

void add_depends(struct patch *new, struct patch *old)
{
	/* patch new depends on patch old, but this hasn't
	 * been recorded yet
	 */
	int size = InitDepends;
	while (size < new->cnt) size<<= 1;

	new->cnt++;
	if (new->cnt > size)
		new->depends = realloc(new->depends, size*sizeof(struct patch *));
	new->depends[new->cnt-1] = old;
}


void add_chunk(struct patch *p, struct file *f, int os, int oe, int ns, int ne)
{
	struct chunk *c = malloc(sizeof(struct chunk));
	c->patch = p;
	c->file = f;
	c->old_start = os;
	c->old_end = oe;
	c->new_start = ns;
	c->new_end = ne;

	for (c1 = f->chunks ; c1 ; c1=c1->next) {
		if (ns < c1->new_end && ne > c1->new_start) {
			/* goody, found a dependancy */
			if (!check_depends(c->patch, c1->patch))
				add_depends(c->patch, c1->patch);
		}
