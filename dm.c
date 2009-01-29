/* dm.c - deathmatch
 * PUBLIC DOMAIN - Jon Mayo - January 29, 2009
 * a tiny unmaintanable telnet game
 */
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#define VER "0.8"
#define S(x) sizeof(x)
#define N(x) (S(x)/S(*x))
#define CK(s) if((s)<0) { perror(#s); exit(1); }
#define F(x) for(a=0;a<N(c);a++) { x; }
#define MAX(a,b) (a)>(b)?(a):(b)
#define CM(f) void f(int b, char *s)
#define CH(f) void f(int b)
#define CD(f) void f(int b, int d)
#define R "\r\n"
#define KS ss(b, B.state);
#define BI(x,d) (((x)>>d)&1)
#define B c[b]
#define A c[a]
#define Rb r[c[b].r]
#define RET return
#define Z(p) memset(p, 0, sizeof *p) /* zero */
#define AP(s,i,p) { strcpy(s+i,p); i+=strlen(p); } /* append */
#define APand(s,i,f) AP(s, i, f>1?", ":f?" and ":" ")
#define NH if(o<0) { wr(b, "That is not here."R); KS; RET; } /* test if not here */
#define LIST_ITEMS(inv, st, vis) \
		for(f=n,j=st;j<N(inv);j++) { \
			if(inv[j].v&&vis) { /* exists and is visible */ \
				f--; \
				AP(tb, o, iname(inv, j)); \
				APand(tb, o, f); \
			} \
		}
#define LIST_PCS(cond) \
		F(if(A.f&&(cond)) { f--; AP(tb, o, A.n); APand(tb, o, f); })
#define D 6 /* number of sides for rolling dice */
#define STA(b, r, f, s...) bc(b, r, "%s" f, B.n, ## s) /* GNU extensions */
#define C_S(f, n, r) \
	CM(f) { \
		scrub(s); \
		pr(b, "You "n" \"%s\"."R, s); \
		STA(b, r, " "n"s \"%s\"."R, s); \
		KS; \
	}
sig_atomic_t tk_fl=1; /* tick timeout flag */
enum {
	lhand, rhand, head, feet, body,
};
struct ii { /* item instance */
	int v, /* vnum */
		fl, /* flags: 0:immobile, 1:invisible */
		a;
};
const char *ln[] = { /* wear location names */
	"right hand", "left hand", "head", "feet", "body",
};
enum {
	hit, def, rec, evade, hpmax, ini
};
const char *sn[] = {
	"Hit", "Defense", "Recovery", "Evade", "Max HP", "Ini"
};
const char *dn[][9]={ /* direction name table and aliases(8 max) */
	{"north", "n"},
	{"south", "s"},
	{"west", "w"},
	{"east", "e"},
	{"up", "u"},
	{"down", "d"}
};
struct { /* client structure */
	int f, r, state, bo, im, /* file descriptor, current room, state, buffer offset, input mode */
		pf, ir, /* prompt flag, last initiative roll */
		hp, xp, t; /* current hit points, experience points, target */
	char n[16], b[999], /* name and buffer */
		s[N(sn)]; /* stats: hit, def, evade, dex, hpmax, ini, ... */
	struct ii i[9]; /* inventory */
} c[16]; /* there is only room in the r.br for int bits of PCs */
const struct { /* item definitions */
	struct ii i; /* initial */
	char *n[9];
	int wl, /* wear location */
		s[N(c->s)], /* stat modifiers */
		hp; /* hitpoint modifiers */
} id[] = {
	{{0}, {"nothing"}},
	{{1}, {"coonskin cap", "cap"}, head, {0, 1}},
	{{2}, {"pair of rubber boots", "rubber boots", "boots"}, feet, {0, 1, -1}},
	{{3}, {"hunting rifle", "rifle"}, rhand, {3}},
	{{4}, {"grannie panties", "panties"}, body, {0, 1}},
	{{5}, {"fist sized rock", "rock"}, rhand, {1}},
	{{6, 3}, {"lever"}},
	{{7}, {"pair of running shoes", "running shoes", "shoes"}, feet, {0, 0, 2}},
	{{8}, {"half eaten slice of pizza", "slice of pizza", "pizza"}, 0, {}, 40},
};
struct {
	int p[N(dn)];
	char *d;
	struct ii i[99]; /* inventory of the room */
	unsigned e, s, /* exit-that-are-doors: EWSN; door state: EWSN; */
		br; /* 1 bit for each participant in a brawl */
} r[] = {
	{{0},"Empty void"},
	{{0,2,0,5}, "A large grassy field."R"To the south is a gate, to the east is a parking lot.", {{1},{3}}, 2, 2},
	{{1,3}, "A large iron gate."R"To the north is a field, to the south is an ampitheater.", {}, 3, 3, },
	{{2,0,4}, "An abandoned ampitheater. There is a large lever near the wall."R"To the north is a gate, to the west is a ticket booth.", {{6, 3}}, 9, 1},
	{{0,0,0,3}, "Ticket booth."R"East exits to the ampitheater.", {{4},{2}, {8}, {8}, {8}}, 4 },
	{{0,0,1}, "A anbandoned parking lot."R"To the west is a grassy field.", {{5},{7}}},
};
char tb[999]; /* temporary buffer */
void scrub(char *s) { /* remove control characters from a string */
	for(;*s;s++) if(!isprint(*s)) *s='#';
}
void wr(int b, const char *s) {
	if(B.f) {
		CK(write(B.f, s, strlen(s)));
		B.pf=1;
	}
}
void pr(int b, const char *s, ...) {
	va_list ap;
	va_start(ap, s);
	vsnprintf(tb, S(tb), s, ap);
	va_end(ap);
	wr(b, tb);
}
void bc(int b, int r, const char *s, ...) { /* broadcast */
	int a;
	va_list ap;
	va_start(ap, s);
	vsnprintf(tb, S(tb), s, ap);
	F(if(a!=b&&A.f&&(r==-1||A.r==r)) { wr(a, tb); });
}
int roll(int c) {
	int r;
	for(r=0;c;c--) r+=(rand()%D+1);
	RET r;
}
int ldn(const char *s) {
	int i, j;
	for(i=0;i<N(dn);i++)
		for(j=0;dn[i][j];j++)
			if(!strcasecmp(s, dn[i][j])) RET i;
	RET -1;
}
char *iname(struct ii *ii, int o) {
	static char b[99];
	char *s=*id[ii[o].v].n;
	if(ii[o].v) {
		snprintf(b, S(b), "%s %s", strchr("aeiou", *s)?"an":"a", s);
	} else {
		strcpy(b, *id->n);
	}
	RET b;
}
int ikw(int n, struct ii *ii, const char *k) { /* find firt keyword match */
	int i, j;
	/* TODO: deal with priorities of keywords */
	for(i=0;i<n;i++) {
		char **s=id[ii[i].v].n;
		for(j=0;s[j];j++) {
			if(ii[i].v&&!strcasecmp(s[j], k)) return i;
		}
	}
	return -1;
}
int inf(int n, struct ii *ii, int skip) { /* return next free inventory slot */
	int i;
	for(i=skip;i<n;i++) {
		if(!ii[i].v) return i;
	}
	return -1;
}
void imv(struct ii *d, struct ii *s) { /* move inventory item from s to d */
	*d=*s;
	s->v=0;
}
void s_add(int b, int *s) { /* apply stats */
	int i;
	for(i=0;i<N(B.s);i++) B.s[i]+=s[i];
}
void s_rem(int b, int *s) { /* remove stats */
	int i;
	for(i=0;i<N(B.s);i++) B.s[i]-=s[i];
}
int idr(int b, struct ii *s) { /* drop item s to room that b is standing in */
	int d;
	d=inf(N(Rb.i), Rb.i, 0);
	if(d<0) { /* room inventory is full */
		wr(b, "No room on the floor."R);
		RET 0;
	}
	pr(b, "You drop %s."R, iname(s, 0));
	STA(b, B.r, " drops %s."R, iname(s, 0));
	if(id[B.i[d].v].wl==d&&d<=body) {
		s_rem(b, id[B.i[d].v].s);
	}
	imv(Rb.i+d, s);
	RET 1;
}
int ckw(int b, int room, const char *s) { /* find firt character in room with name */
	int a;
	F(if(A.f&&A.r==room&&!strcasecmp(s, A.n)) { RET a; });
	wr(b, "I don't see that here."R);
	RET -1;
}
int ist(int b, int d) { /* store into inventory */
	if(B.i[d].v) { /* clear out the left hand */
		int e=inf(N(B.i), B.i, body+1); /* find an available inventory location */
		if(e<0) { /* inventory full */
			wr(b, "Your inventory is full!"R);
			if(!idr(b, B.i+d)) { /* drop it on the floor */
				RET 0;
			}
			RET 1;
		}
		pr(b, "You put away %s."R, iname(B.i, d));
		STA(b, B.r, " puts away %s."R, iname(B.i, d));
		if(id[B.i[d].v].wl==d&&d<=body) {
			s_rem(b, id[B.i[d].v].s);
		}
		imv(B.i+e, B.i+d);
	}
	RET 1;
}
void inv(int a, int b) {
	char x[99]="You are ";
	int j, o, f, n;
	if(a!=b) {
		snprintf(x, S(x), "%s is ", B.n);
	}
	pr(a, "%swielding %s.\n", x, iname(B.i, rhand));
	pr(a, "%sholding %s.\n", x, iname(B.i, lhand));
	for(j=head;j<=body;j++) {
		pr(a, "%swearing %s on %s %s.\n", x, iname(B.i, j), a==b?"your":"his", ln[j]);
	}

	*tb=o=0;
	if(a!=b) { AP(tb, o, B.n); AP(tb, o, "'s"); } else AP(tb, o, "Your");
	AP(tb, o, " pack contains ");
	for(n=0,j=body+1;j<N(B.i);j++) if(B.i[j].v) n++;
	LIST_ITEMS(B.i, body+1, 1);
	if(n) o--;
	AP(tb, o, n?".":*id->n);
	AP(tb, o, R);
	wr(a, tb);
}
CH(stop_brawl) {
	int a=B.t;
	Rb.br&=~(1<<b);
	if(a>=0) {
		pr(b, "You stop fighting %s."R, A.n);
	}
	/* search for anyone else who is attacking you,
	 * also stop the brawl if they're the only one left in it. */
	F(if(A.t==b) { r[A.r].br&=~(1<<b); A.t=-1; if(!(r[A.r].br&~(1<<a))) r[A.r].br=0; pr(a, "You stop fighting %s."R, B.n); bc(a, A.r, "%s and %s stop fighting."R, B.n, A.n); });
	B.t=-1;
}
CH(cl) {
	int o, d;
	pr(b, "Your score was: %d\n", B.xp);
	close(B.f);
	B.f=0;
	STA(b, -1, " has disconnected."R);
	/* stop brawling */
	stop_brawl(b);
	if(B.t>=0) {
		stop_brawl(B.t); /* BUG: this takes someone out of the brawl */
	}
	/* drop all the inventory items */
	for(o=0;o<N(B.i);o++) {
		if(B.i[o].v) {
			d=inf(N(Rb.i), Rb.i, 0);
			if(d<0) {
				STA(b, -1, "'s %s was destroyed."R, iname(B.i, o));
				B.i[o].v=0;
			} else {
				idr(b, B.i+o);
			}
		}
	}
}
CD(ss) {
	B.state=d;
	if(d==0) {
		pr(b, "#%03u|hp:%d/%d|xp:%d> ", B.r, B.hp, B.s[hpmax]*D, B.xp);
	}
	B.pf=0;
}
CM(c_q) {
	cl(b);
}
CM(c_lr) {
	int a, f, j, o=0, n;
	/* room description */
	pr(b, "%s"R, Rb.d);
	/* check for items on the floor */
	for(n=0,j=0;j<N(Rb.i);j++) if(Rb.i[j].v&&~Rb.i[j].fl&2) n++;
	if(n) {
		LIST_ITEMS(Rb.i, 0, ~Rb.i[j].fl&2);
		strcpy(tb+o, n==1?"is laying here."R:"are laying here."R);
		wr(b, tb);
	}
	/* check for players */
	*tb=o=n=0;
	F(if(a!=b&&A.f&&A.r==B.r) { n++; });
	if((f=n)) {
		LIST_PCS(a!=b&&A.r==B.r)
		AP(tb, o, n==1?"is standing here."R:"are standing here."R);
		wr(b, tb);
	}
}
CM(c_l) {
	if(*s) { /* look at user */
		int a=ckw(b, B.r, s);
		if(a>=0) {
			if(b!=a) {
				pr(a, "%s looks at you."R, B.n);
				pr(b, "You look at %s."R, A.n);
			}
			inv(b, a);
		}
	} else { /* look at room */
		c_lr(b, s);
	}
	KS;
}
CD(go) {
	int t=Rb.p[d];
	if(!t) {
		wr(b, "You can't go that way!"R);
	} else if(BI(Rb.e&Rb.s,d)) {
		wr(b, "The door is closed."R);
	} else if(B.t>=0) {
		pr(b, "You are currently engaged in a fight with %s!"R, c[B.t].n);
	} else {
		STA(b, B.r=t," leaves."R);
		STA(b, t, " enters."R);
		c_l(b, "");
		RET;
	}
	KS;
	RET;
}
C_S(c_say, "say", B.r);
C_S(c_shout, "shout", -1);
CM(c_h);
CM(c_name) {
	int a;
	char *p;
	if(strlen(s)>=S(c->n)) s[S(c->n)-1]=0;
	for(p=s;*p;p++) if(!isalnum(*p)) *p='_';
	if(*s) {
		F(if(a!=b&&A.f&&A.r==B.r&&!strncmp(A.n, s, S(c->n))) { wr(b, "Name already used."R); KS; RET; } );

		STA(b, -1, " is now known as %s."R, s);
		strcpy(B.n, s);
		pr(b, "Your name is now %s."R, B.n);
	} else {
		pr(b, "Your name is %s."R, B.n);
	}
	KS;
}
CM(c_o) {
	int d=ldn(s);
	if(d>=0&&BI(Rb.e&Rb.s, d)) {
		int t=Rb.p[d];
		Rb.s^=1<<d;
		r[t].s^=1<<(d^1);
		wr(b, "You open the exit."R);
		STA(b, B.r, " opens the exit."R);
		STA(b, t, " opens the exit."R);
	} else {
		wr(b, "You can't open that."R);
	}
	KS;
}
CM(c_c) {
	int d=ldn(s);
	if(d>=0&&BI(Rb.e, d)&&!BI(Rb.s, d)) {
		int t=Rb.p[d];
		Rb.s^=1<<d;
		r[t].s^=1<<(d^1);
		wr(b, "You close the exit."R);
		STA(b, B.r, " closes the exit."R);
		STA(b, t, " closes the exit."R);
	} else {
		wr(b, "You can't close that."R);
	}
	KS;
}
void wh(int b) {
	int a, n=0;
	pr(b, "XP    Name"R);
	F(if(A.f) { n++; pr(b, "%-5u %-16s"R, A.xp, A.n); });
	pr(b, "***"R"%u user%s online."R, n, n==1?"":"s");
}
CM(c_wh) {
	wh(b);
	KS;
}
CM(c_ex) {
	int d, e;
	for(d=0;d<N(Rb.p);d++) {
		if((e=Rb.p[d])>0) {
			wr(b, dn[d][0]);
			if(BI(Rb.e, d)) wr(b, BI(Rb.s, d)?"(closed)":"(open)");
			wr(b, " ");
		}
	}
	wr(b, R);
	KS;
}
CM(c_t) {
	int fr=0, o=ikw(N(Rb.i), Rb.i, s);
	if(o<0) { o=ikw(N(B.i), B.i, s); fr=1; }
	NH;
	if(!fr&&Rb.i[o].fl&1) {
		wr(b, "It is stuck to the floor!"R);
		KS;
		RET;
	}
	if(fr&&o==lhand) {
		pr(b, "You are already holding %s."R, iname(B.i, lhand));
		KS;
		RET;
	}
	if(!ist(b, lhand)) {
		KS;
		RET;
	}
	if(fr) {
		pr(b, "You hold %s."R, iname(B.i, o));
		STA(b, B.r, " holds %s."R, iname(B.i, o));
		imv(B.i+lhand, B.i+o);
	} else {
		pr(b, "You pick up %s."R, iname(Rb.i, o));
		STA(b, B.r, " picks up %s."R, iname(Rb.i, o));
		imv(B.i+lhand, Rb.i+o);
	}
	KS;
}
CM(c_dr) { /* "drop" command */
	int o=ikw(N(B.i), B.i, s);
	NH;
	if(!idr(b, B.i+o)) {
		wr(b, "No space on the floor."R);
	}
	KS;
}
CM(c_wear) {
	int o=ikw(N(B.i), B.i, s);
	if(o>=0) {
		int v=B.i[o].v, wl=id[v].wl;
		if(o==wl) {
			wr(b, "You are already wearing that."R);
			KS; RET;
		} else if((wl>lhand&&wl<=body)&&ist(b, wl)) {
			imv(B.i+wl, B.i+o);
			pr(b, "You wear %s."R, iname(B.i, wl));
			STA(b, B.r, " wears %s."R, iname(B.i, wl));
			s_add(b, id[v].s);
			KS; RET;
		}
	}
	wr(b, o<0?"You don't have that."R:"You can't wear that."R);
	KS;
}
CM(c_rem) {
	int o=ikw(N(B.i), B.i, s);
	if(o>=0) {
		int v=B.i[o].v, wl=id[v].wl;
		if(wl==o&&ist(b, wl)) {
			KS;
			RET;
		}
	}
	wr(b, o<0?"You don't have that."R:"You can't remove that."R);
	KS;
}
CM(c_i) {
	inv(b, b);
	KS;
}
CM(c_pull) {
	int o=ikw(N(Rb.i), Rb.i, s);
	NH;
	pr(b, "You pull %s."R, iname(Rb.i, o));
	/* TODO: activate item's handler - door lock, etc. */
	KS;
}
CM(c_push) {
	int o=ikw(N(Rb.i), Rb.i, s);
	NH;
	pr(b, "You push %s."R, iname(Rb.i, o));
	/* TODO: activate item's handler - soda machine, etc */
	KS;
}
CM(c_score) {
	int i;
	wr(b, "Character sheet:"R);
	for(i=0;i<N(sn);i++) {
		if(i!=hpmax)
			pr(b, "  %-5s: %d"R, sn[i], B.s[i]*D);
		else
			pr(b, "  %-5s: %d"R, "HP", B.hp, B.s[i]*D);
	}
	pr(b, "  %-5s: %d"R, "XP", B.xp);
	KS;
}
CM(c_kill) {
	if(*s) {
		int a=ckw(b, B.r, s);
		if(a>=0) {
			if(b!=a) {
				/* TODO: pull target into a brawl */
				if(B.t==a&&BI(Rb.br, a)&&BI(Rb.br, b)) {
					wr(b, "You are already targeting them."R);
				} else {
					Rb.br|=(1<<b)|(1<<a);
					if(B.t>=0) {
						STA(b, B.r, " stops attacking %s to attack %s!"R, c[B.t].n, A.n);
						pr(b, "You stop attacking %s to attack %s!"R, c[B.t].n, A.n);
					} else {
						pr(a, "%s just attacked you!"R, B.n);
						bc(a, B.r, "%s and %s start fighting!"R, B.n, A.n);
					}
					B.t=a;
					if(A.t<0) {
						A.t=b;
						pr(a, "You attack %s."R, B.n);
					}
				}
			} else {
				wr(b, "You can't fight yourself."R);
			}
		}
	} else {
		wr(b, "You must choose a target."R);
	}
	KS;
}
CM(c_flee) {
	int i;
	if(!*s||(i=ldn(s))<0) {
		pr(b, "You must pick a direction to flee."R);
		KS; RET;
	}
	if(B.t<0) {
		wr(b, "You are not fighting anyone!"R);
		KS; RET;
	}
	stop_brawl(b);
	go(b, i);
}
CM(c_br) { /* show all brawls */
	int a, j, o, f, n, k=1;
	for(j=0;j<N(r);j++) {
		if(r[j].br) {
			if(k) {
				wr(b, "Brawls:"R);
				k=0;
			}
			n=0;
			F(if(A.f&&BI(r[j].br, a)) { n++; });
			o=sprintf(tb, "Room #%-3u : ", j);
			if((f=n)) {
				LIST_PCS(BI(r[j].br, a));
				AP(tb, o, "are fighting."R);
				wr(b, tb);
			}
		}
	}
	if(k)
		wr(b, "There are currently no brawls."R);
	KS;
}
CM(c_eat) {
	int o=ikw(N(B.i), B.i, s);
	if(o>=0) {
			pr(b, "You eat %s."R, iname(B.i, o));
			STA(b, B.r, " eats %s."R, iname(B.i, o));
			B.hp=MAX(B.hp+id[B.i[o].v].hp, B.s[hpmax]);
			B.i[o].v=0; /* deleted */
			RET; KS;
	} else
		wr(b, "You don't have that."R);
	KS;
}
const struct {
	char *n[9];
	CM((*f));
} ct[] = { /* command table */
	{{"quit"}, c_q},
	{{"look", "l"}, c_l},
	{{"say", "\""}, c_say},
	{{"shout", "!"}, c_shout},
	{{"help", "?"}, c_h},
	{{"name"}, c_name},
	{{"open", "o"}, c_o},
	{{"close", "c"}, c_c},
	{{"who"}, c_wh},
	{{"exits", "ex"}, c_ex},
	{{"take", "t", "get", "g"}, c_t},
	{{"drop", "dr"}, c_dr},
	{{"inventory", "i", "inv"}, c_i},
	{{"pull"}, c_pull},
	{{"push"}, c_push},
	{{"wear", "eq"}, c_wear},
	{{"remove", "r"}, c_rem},
	{{"score", "sc"}, c_score},
	{{"kill", "k", "hit", "fight"}, c_kill},
	{{"flee", }, c_flee},
	{{"brawls", "br", }, c_br},
	{{"eat", }, c_eat},
};
CM(c_h) {
	int i, j;
	for(i=0;i<N(dn);i++) {
		for(j=0;dn[i][j];j++) {
			wr(b, dn[i][j]);
			wr(b, " ");
		}
	}
	for(i=0;i<N(ct);i++) {
		for(j=0;ct[i].n[j];j++) {
			wr(b, ct[i].n[j]);
			wr(b, " ");
		}
	}
	wr(b, R);
	KS;
}
CH(sl) {
	char *p=B.b, tmp[2]={0};
	int i, j, l;
	if(B.b[0]) {
		/* printf("%d:input '%s'\n", b, B.b); */
		if((i=ldn(B.b))>=0) {
			go(b, i);
			goto x;
		}
		if(ispunct(*B.b)) {
			*tmp=*B.b;
			p=tmp;
		} else {
			l=strcspn(B.b," ");
			if(B.b[l]) B.b[l++]=0;
		}
		while(isspace(p[l])) l++;
		for(i=0;i<N(ct);i++) {
			for(j=0;ct[i].n[j];j++) {
				if(!strcmp(p, ct[i].n[j])) {
					ct[i].f(b, B.b+l);
					goto x;
				}
			}
		}
		wr(b, "Try 'help' for commands."R);
	}
	KS;
x:
	B.b[B.bo=0]=0;
	B.im=0;
}
CD(nc) {
	Z(&B);
	B.f=d;
	B.t=-1; /* not targeting anyone */
	B.r=1; /* starting room */
	memset(B.s, 6, S(B.s)); /* initialize basic stats */
	B.hp=B.s[hpmax]*D;
	snprintf(B.n, S(B.n), "Someone%u", b+1);
	wr(b, "Welcome to Deathmatch!"R
	"*** version "VER" ***"R
	"You can get the source at http://github.com/OrangeTide/dm"R
	"Type 'help' for help."R);
	wh(b);
	c_l(b, "");
	STA(b, B.r, " enters."R);
}
CM(in) { /* received a character */
	unsigned char k=*s;
	if(B.im) {
		char rs[4]={'\377',0,k,0}; /* response buffer */
		switch(B.im) {
			case 1:
				if(k==255) { AP(B.b, B.bo, s); B.im=0; } /* IAC IAC */
				else if(k==254) B.im=2; /* IAC DONT */
				else if(k==253) B.im=3; /* IAC DO */
				else if(k==252) B.im=4; /* IAC WONT */
				else if(k==251) B.im=5; /* IAC WILL */
				else if(k==250) cl(b); /* IAC SB not supported */
				else B.im=0; /* IAC x - printf("IAC %hhu\n", *s); */
				break;
			case 2: /* DONT */
				/* printf("IAC DONT %hhu\n", *s); */
				B.im=0;
				break;
			case 3: /* DO */
				/* printf("IAC DO %hhu\n", *s); */
				rs[1]='\374';
				wr(b, rs);
				B.im=0;
				break;
			case 4: /* WILL */
				/* printf("IAC WONT %hhu\n", *s); */
				B.im=0;
				break;
			case 5: /* WONT */
				/* printf("IAC WILL %hhu\n", *s); */
				rs[1]='\376';
				wr(b, rs);
				B.im=0;
				break;
		}
	} else if(k==255) /* saw IAC */
		B.im++;
	else if((k=='\b'||k==127)&&B.bo)  /* backspace */
		B.b[--B.bo]=0;
	else if(k=='\r')
		sl(b);
	else if(k=='\n') {
		if(B.bo) sl(b); /* HACK - handle LF only if the line is not empty */
	} else if(B.bo+2>S(B.b))
		cl(b); /* input buffer overflow */
	else
		AP(B.b, B.bo, s); /* append character to buffer*/
}
int cmp_ir(const void *p, const void *q) { /* compare initiative rolls */
	int a=*(int*)p, b=*(int*)q;
	RET B.ir-A.ir;
}
void combat(void) {
	int l[N(c)]; /* client list */
	int a, b, d, i, n=0, nhp;
	F(if(A.f) { l[n++]=a; A.ir=roll(A.s[ini]); }); /* calculate initiative */
	qsort(l, n, S(*l), cmp_ir); /* sort based on initiatives */
	/* apply combat */
	for(i=0;i<n;i++) {
		a=l[i]; /* a:attacker b:defender */
		nhp=roll(A.s[rec])/D;
		if(nhp) {
			A.hp=MAX(A.hp+nhp, A.s[hpmax]); /* recover hitpoints */
			wr(a, R); /* cause prompt update */
		}
		if((b=A.t)>=0) {
			d=roll(A.s[hit])-roll(B.s[def]);
			if(d>0) {
				pr(a, "You hit %s for %d points of damage."R, B.n, d);
				bc(a, B.r, "%s hits %s for %d points of damage."R, A.n, B.n, d);
				B.hp-=d;
				if(B.hp<0) {
					A.xp+=B.xp+1;
					pr(b, "You die from %s's attack!!"R, A.n);
					bc(b, -1, "%s dies from %s's attack!!"R, B.n, A.n);
					cl(b);
				}
			} else {
				pr(a, "You miss %s."R, B.n);
				bc(a, B.r, "%s misses %s."R, A.n, B.n);
			}
		}
	}
}
void sh_tk(int s) { tk_fl=1; } /* signal handler for ticks */
int main(int ac, char **av) {
	int s, a, n, m, t;
	struct sockaddr_in x;
	fd_set z;
	socklen_t l;
	char h[2]={0};
	struct sigaction sa;
	Z(&sa);
	sa.sa_handler=sh_tk;
	sa.sa_flags=SA_RESTART;
	CK(sigaction(SIGALRM, &sa, 0));
	CK(s=socket(PF_INET, SOCK_STREAM, 0));
	Z(&x);
	x.sin_port=htons(ac>1?atoi(av[1]):5555);
	CK(bind(s, (struct sockaddr*)&x, S(x)));
	CK(listen(s,1));
	for(;;) {
		FD_ZERO(&z);
		FD_SET(s, &z);
		if(tk_fl>0) { /* process tick */
			combat();
			tk_fl=0;
			alarm(3); /* this is the tick rate */
		}
		m=s; /* calculate max */
		F(if(A.f) { if(A.pf) ss(a, A.state); FD_SET(A.f, &z); if(A.f>m) m=A.f; });
		if(m==s) {
			puts("Zzz"); /* sleeping - no clients */
			tk_fl=-1; /* disable ticks, go to sleep */
			alarm(0);
		} else if(tk_fl<0) {
			tk_fl=1; /* force a tick */
			continue;
		}
		n=select(m+1, &z, 0, 0, 0);
		if(n<0) {
			if(errno==EINTR) continue;
			CK(-1);
		}
		if(FD_ISSET(s, &z)) {
			l=S(x);
			CK(t=accept(s, (struct sockaddr*)&x, &l));
			F(if(!A.f) { nc(a, t); break; }); /* find next free slot */
			if(a==N(c)) {
				close(t);
			}
		}
		F(if(A.f&&FD_ISSET(A.f, &z)) { CK(t=read(A.f, h, 1)); if(t==0) cl(a); else in(a, h); } );
	}
	RET 0;
}
