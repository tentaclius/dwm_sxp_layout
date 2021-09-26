/****
;; examples
;; {{{

h (master weight: 1.2) (v ...)  ; classic tile:
h (v weight: 1.2 (c weight: 1.2) 1) (v (rest))  ; two weighted clients in the master area:
h c (v 3)  ; master + 3 clients in the stack area
v ...  ; all clients in a column
h c (v (max 3) (w ...))  ; one master + four slots in the stack area. Every other window will be stacked in the last frame.
h (nth 1) (nth 0)  ; display first two clients from the list, the first after the second.

;; }}}

;; elements
...         ; all the leftover windows
c           ; client slot
(max <num>) ; at most <num> clients
(nth <num>) ; client by it's position from the top of the client stack

;; parameters
w: weight:  ; weight of the given client or node
f: x y w h  ; floating gemoetry

;; containers
(h ...)
(v ...)
(m ...)  ; all clients inside will be located to the same viewport, i.e. monocle
***/

/***
 * TODO:
 * - Window margin support;
 * - Floating nodes (h, v, m)
***/

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "util.h"

enum node_type_t
{
   ND_NULL,

   // Containers
   ND_MONOCLE,
   ND_HORIZONTAL_LR,
   ND_HORIZONTAL_RL,
   ND_VERTICAL_UD,
   ND_VERTICAL_DU,

   // Elements
   ND_CLIENT,
   ND_CLIENT_NUM,
   ND_CLIENT_NTH,
   ND_CLIENT_FLOAT,
   ND_REST,
};

typedef struct node_t node_t;
struct node_t
{
   enum node_type_t type;
   float weight;
   int x, y, w, h;
   int f;
   unsigned n;
   Client *c;
   struct node_t *branch;
   struct node_t *next;
};

struct client_ref_t
{
   Client *c;
   struct client_ref_t *next;
};

node_t *s_layout_scheme;

node_t* alloc_node(enum node_type_t type)
{
   node_t *node = (node_t*) malloc(sizeof(node_t));
   memset(node, 0, sizeof(node_t));
   node->type = type;
   return node;
}

node_t* clone_node(node_t *n)
{
   if (n == NULL)
      return NULL;

   node_t *node = alloc_node(n->type);
   node->weight = n->weight;
   node->x = n->x;
   node->y = n->y;
   node->w = n->w;
   node->h = n->h;
   node->f = n->f;
   node->n = n->n;
   node->c = n->c;
   node->next = NULL;
   node->branch = NULL;
   return node;
}

int is_nested(node_t *node)
{
   return node->type == ND_HORIZONTAL_LR
       || node->type == ND_HORIZONTAL_RL
       || node->type == ND_VERTICAL_UD
       || node->type == ND_VERTICAL_DU
       || node->type == ND_MONOCLE;
}

void free_node(node_t *node)
{
   for ( node_t *n = node;
         n != NULL; )
   {
      if (is_nested(n))
         free_node(n->branch);

      node_t *ns = n->next;

      free(n);
      n = ns;
   }
}

node_t* reverse_node(node_t *node)
{
   node_t *a = node, *b = NULL;

   if (a != NULL) {
      b = a->next;
      a->next = NULL;
   }

   while (b != NULL) {
      node_t *nx = b->next;
      b->next = a;
      a = b;
      b = nx;
   }

   return (b ? b : a);
}

void node_length(node_t *node, unsigned *len, float *weight)
{
   unsigned n = 0;
   float w = 0.0;

   for (; node != NULL; node = node->next) {
      if (!node->f) {
         n ++;
         w += (node->weight == 0 ? 1 : node->weight);
      }
   }

   if (len != NULL)
      *len = n;

   if (weight != NULL)
      *weight = w;
}

struct client_ref_t* copy_clients(Client *clients)
{
   struct client_ref_t head;
   head.next = NULL;
   struct client_ref_t *tail = &head;

   for (Client *c = nexttiled(clients); c != NULL; c = nexttiled(c->next)) {
      tail->next = (struct client_ref_t*) malloc(sizeof(struct client_ref_t));
      tail = tail->next;
      
      tail->next = NULL;
      tail->c = c;
   }

   return head.next;
}

void free_clients(struct client_ref_t *clients)
{
   struct client_ref_t *nxt = NULL;
   while (clients != NULL) {
      nxt = clients->next;
      free(clients);
      clients = nxt;
   }
}

struct s_recur_analyze_ret {
   node_t *head;
   node_t *tail;
}
s_recur_analyze(struct client_ref_t **clients, node_t *node)
{
   struct client_ref_t *c = *clients;
   unsigned i = 0;

   // A single client, just assign a client.
   if (node->type == ND_CLIENT) {
      struct s_recur_analyze_ret ret;
      ret.tail = ret.head = clone_node(node);
      *clients = c->next;
      ret.head->c = c->c;
      return ret;
   }

   // Pick n'th client from the list top.
   if (node->type == ND_CLIENT_NTH) {
      struct client_ref_t *prev = NULL;
      struct s_recur_analyze_ret ret;
      ret.head = ret.tail = NULL;

      for ( i = 0, c = *clients;
            i < node->n && c != NULL;
            i ++, c = c->next) 
      {
         prev = c;
      }

      if (c != NULL) {
         if (prev == NULL)
            *clients = c->next;
         else
            prev->next = c->next;

         ret.tail = ret.head = clone_node(node);
         ret.head->type = ND_CLIENT;
         ret.head->c = c->c;
      }

      return ret;
   }

   // Fixed number of clients.
   if (node->type == ND_CLIENT_NUM) {
      struct s_recur_analyze_ret ret;
      node_t head, *p = &head;
      head.next = NULL;
      for ( i = 0, c = *clients;
            i < node->n && c != NULL;
            i ++, c = c->next )
      {
         p->next = clone_node(node);
         p = p->next;
         p->type = ND_CLIENT;
         p->c = c->c;
      }
      *clients = c;
      ret.head = head.next;
      ret.tail = p;
      return ret;
   }

   // All leftover client.
   if (node->type == ND_REST) {
      struct s_recur_analyze_ret ret;
      node_t head, *p = &head;
      head.next = NULL;
      for (c = *clients; c != NULL; c = c->next) {
         p->next = clone_node(node);
         p = p->next;
         p->type = ND_CLIENT;
         p->c = c->c;
      }
      *clients = c;
      ret.head = head.next;
      ret.tail = p;
      return ret;
   }

   if (node->type == ND_NULL) {
      struct s_recur_analyze_ret ret;
      ret.head = NULL;
      ret.tail = NULL;
      return ret;
   }

   // In case the element is a container
   if (is_nested(node)) {
      struct s_recur_analyze_ret ret;
      ret.head = clone_node(node);
      ret.tail = ret.head;

      struct s_recur_analyze_ret x;
      x.head = ret.head;
      x.tail = x.head;

      node_t branch, *tail = &branch;
      branch.next = NULL;

      node_t *n = NULL;

      // For reversed containers the order must be reversed
      if (node->type == ND_HORIZONTAL_RL || node->type == ND_VERTICAL_DU) {
         n = reverse_node(node->branch);
      } else {
         n = node->branch;
      }

      for (; *clients != NULL && n != NULL; n = n->next )
      {
         x = s_recur_analyze(clients, n);
         
         // Attach the received tree to the tail of the previous element
         if (x.head != NULL) {
            tail->next = x.head;
            tail = x.tail;
         }
      }

      if (node->type == ND_HORIZONTAL_RL || node->type == ND_VERTICAL_DU) {
         ret.head->branch = reverse_node(branch.next);
      } else {
         ret.head->branch = branch.next;
      }

      return ret;
   }

   struct s_recur_analyze_ret ret;
   ret.head = NULL;
   ret.tail = NULL;
   return ret;
}

struct frame_t
{
   int x, y, w, h;
};

void s_recur_resize(node_t *node, struct frame_t frame)
{
   if (node == NULL) return;

   if (node->type == ND_CLIENT) {
      if (node->f)
         resize(node->c, node->x, node->y, node->w, node->h, 0);
      else
         resize(node->c, frame.x, frame.y, frame.w, frame.h, 0);
      return;
   }

   if (node->type == ND_VERTICAL_UD || node->type == ND_VERTICAL_DU) {
      unsigned len = 0;
      float wgt = 0.0;
      int delta = 0;
      float avg_wgt = 1;
      
      node_length(node->branch, &len, &wgt);
      if (len != 0) {
         delta = frame.h / len;
         avg_wgt = wgt / len;
      }

      for (node_t *n = node->branch; n != NULL; n = n->next) {
         if (!n->f) {
            frame.h = (n->weight == 0 ? 1 : n->weight) / avg_wgt * delta;
            s_recur_resize(n, frame);
            frame.y += frame.h;
         } else {
            s_recur_resize(n, frame);
         }
      }
      return;
   }

   if (node->type == ND_HORIZONTAL_RL || node->type == ND_HORIZONTAL_LR) {
      unsigned len = 0;
      float wgt = 0.0;
      int delta = 0;
      float avg_wgt = 1;
      
      node_length(node->branch, &len, &wgt);
      if (len != 0) {
         delta = frame.w / len;
         avg_wgt = wgt / len;
      }

      for (node_t *n = node->branch; n != NULL; n = n->next) {
         if (!n->f) {
            frame.w = (n->weight == 0 ? 1 : n->weight) / avg_wgt * delta;
            s_recur_resize(n, frame);
            frame.x += frame.w;
         } else {
            s_recur_resize(n, frame);
         }
      }
      return;
   }

   if (node->type == ND_MONOCLE) {
      for (node_t *n = node->branch; n != NULL; n = n->next)
         s_recur_resize(n, frame);
   }
}

// Main layout function.
void s_layout(Monitor *m)
{
   // Need to clone the client stack, as we might need to pull items from it.
   struct client_ref_t *clients = copy_clients(m->clients),
                       *clients_root = clients;

   if (s_layout_scheme == NULL)
      return;

   struct s_recur_analyze_ret ret = s_recur_analyze(&clients, s_layout_scheme);

   struct frame_t frame;
   frame.x = m->wx;
   frame.y = m->wy;
   frame.w = m->ww;
   frame.h = m->wh;

   s_recur_resize(ret.head, frame);

   // Free the resources we allocated.
   free_clients(clients_root);
   free_node(ret.head);
}

enum s_char_type_t {
   SC_SPACE,
   SC_WORD,
   SC_PAREN,
   SC_EOF,
};

enum s_char_type_t
char_type(char c)
{
   if (c == ' ' || c == '\t' || c == '\n')
      return SC_SPACE;

   if (c == '(' || c == ')')
      return SC_PAREN;

   if (c == '\0')
      return SC_EOF;

   return SC_WORD;
}

// Tokenize string
struct string_token_t {
   char token[8];
   struct string_token_t *next;
};

struct string_token_t* tokenize_string(char *str)
{
   struct string_token_t head;
   struct string_token_t *node = &head;
   head.next = NULL;
   unsigned word_start = UINT_MAX;
   unsigned len = 0;

   for (unsigned i = 0;; i ++) {
      switch (char_type(str[i])) {
         case SC_EOF:
            if (word_start != UINT_MAX) {
               node->next = (struct string_token_t*) malloc(sizeof(struct string_token_t));
               node = node->next;
               node->next = NULL;
               len = MIN(i - word_start, sizeof(node->token) - 1);
               strncpy(node->token, &str[word_start], len);
               node->token[len] = '\0';
            }
            return head.next;

         case SC_SPACE:
         case SC_PAREN:
            if (word_start != UINT_MAX) {
               node->next = (struct string_token_t*) malloc(sizeof(struct string_token_t));
               node = node->next;
               node->next = NULL;
               len = MIN(i - word_start, sizeof(node->token) - 1);
               strncpy(node->token, &str[word_start], len);
               node->token[len] = '\0';
               word_start = UINT_MAX;
            }
            if (char_type(str[i]) == SC_PAREN) {
               node->next = (struct string_token_t*) malloc(sizeof(struct string_token_t));
               node = node->next;
               node->next = NULL;
               node->token[0] = str[i];
               node->token[1] = '\0';
            }
            break;

         default:
            if (word_start == UINT_MAX)
               word_start = i;
      }
   }
   return NULL;
}

// Parse s-expression to node_t structure
node_t* parse_sexp(struct string_token_t **token)
{
   node_t *head = NULL;
   node_t branch, *p = &branch;
   branch.next = NULL;

   struct string_token_t *t = *token;

   while (t != NULL) {
      if (strcmp(t->token, ")") == 0) {
         t = t->next;
         break;
      }

      if (strcmp(t->token, "(") == 0) {
         t = t->next;
         *token = t;

         if (head == NULL) {
            head = parse_sexp(token);
         } else {
            p->next = parse_sexp(token);
            if (p->next) p = p->next;
         }
         t = *token;
         continue;
      }

      // ==== Client slots
      // Single client
      if (strcmp(t->token, "c") == 0) {
         if (head == NULL) {
            head = alloc_node(ND_CLIENT);
         } else {
            p->next = alloc_node(ND_CLIENT);
            p = p->next;
         }
         t = t->next;
         continue;
      }

      // The rest of the clients
      if (strcmp(t->token, "...") == 0) {
         if (head == NULL) {
            head = alloc_node(ND_REST);
         } else {
            p->next = alloc_node(ND_REST);
            p = p->next;
         }
         t = t->next;
         continue;
      }
      
      // N'th client
      if (strcmp(t->token, "nth") == 0) {
         if (head == NULL) {
            head = alloc_node(ND_CLIENT_NTH);

            t = t->next;
            if (t != NULL) {
               head->n = (unsigned) atoi(t->token);
               t = t->next;
            }
         }
         continue;
      }

      // Max n clients
      if (strcmp(t->token, "max") == 0) {
         if (head == NULL) {
            head = alloc_node(ND_CLIENT_NUM);

            t = t->next;
            if (t != NULL) {
               head->n = (unsigned) atoi(t->token);
               t = t->next;
            }
         }

         continue;
      }

      // ==== Parameters
      // weight
      if (strcmp(t->token, "w:") == 0 && head != NULL) {
         t = t->next;

         if (t != NULL) {
            head->weight = (float) atof(t->token);
            t = t->next;
         }
         continue;
      }

      // floating geometry
      if (strcmp(t->token, "f:") == 0 && head != NULL) {
         head->f = 1;
         t = t->next;

         if (t != NULL) {
            head->x = atoi(t->token);
            t = t->next;
         }
         if (t != NULL) {
            head->y = atoi(t->token);
            t = t->next;
         }
         if (t != NULL) {
            head->w = atoi(t->token);
            t = t->next;
         }
         if (t != NULL) {
            head->h = atoi(t->token);
            t = t->next;
         }
         continue;
      }

      // ==== Containers
      if (strcmp(t->token, "h") == 0 && head == NULL)
         head = alloc_node(ND_HORIZONTAL_LR);

      if (strcmp(t->token, "hr") == 0 && head == NULL)
         head = alloc_node(ND_HORIZONTAL_RL);

      if (strcmp(t->token, "v") == 0 && head == NULL)
         head = alloc_node(ND_VERTICAL_UD);

      if (strcmp(t->token, "vr") == 0 && head == NULL)
         head = alloc_node(ND_VERTICAL_UD);

      if (strcmp(t->token, "m") == 0 && head == NULL)
         head = alloc_node(ND_MONOCLE);

      t = t->next;
   }

   if (head)
      head->branch = branch.next;

   *token = t;
   return head;
}

#define SXP_HISTORY "/tmp/.dwm_sxp_history"
void set_s_layout(const Arg *arg)
{
   // make sure the history file exists
   FILE *hf = fopen(SXP_HISTORY, "a");
   fclose(hf);
   system("sort " SXP_HISTORY " | uniq > " SXP_HISTORY "~");
   system("mv " SXP_HISTORY "~ " SXP_HISTORY);

   FILE *pp = popen("dmenu -i -l 10 -p 'sxp>' <" SXP_HISTORY, "r");
   if (!pp) return;
   char buf[1024 + 1];
   buf[1024] = '\0';
   fgets(buf, 1024, pp);
   fclose(pp);
   if (buf[0] == '\0') return;

   // Write to history file
   hf = fopen(SXP_HISTORY, "a");
   fprintf(hf, "%s", buf);
   fclose(hf);

   if (s_layout_scheme != NULL) {
      free_node(s_layout_scheme);
      s_layout_scheme = NULL;
   }

   struct string_token_t *token_root = tokenize_string(buf),
                         *token = token_root;

   s_layout_scheme = parse_sexp(&token);

   setlayout(arg);

   // Free the token list
   while (token_root != NULL) {
      token = token_root->next;
      free(token_root);
      token_root = token;
   }
}
