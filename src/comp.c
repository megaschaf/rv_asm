
#include<string.h>
#include<elf.h>

#include"comp.h"
#include"util.h"
#include"export.h"

void setSection(CompContext*ctx,uint32_t type,uint32_t flags){
  if(ctx->pass == INDEX_SECTIONS){
    ctx->shstrtab->size += ctx->token->buffTop - ctx->token->buff + 1;
    return;
  }

  Section*sec = ctx->shstrtab;
  // Try to select existing section
  while(sec){
    if(tokenIdentComp((char*)(ctx->shstrtab->buff + sec->name_offset), ctx->token)){
      ctx->section = sec;
      return;
    }
    sec=sec->next;
  }
  // Create New Section
  if(ctx->pass == INDEX_BUFFERS){
    // Allocate Section
    sec = malloc(sizeof(Section));
    sec->index=0;
    sec->size=0;
    sec->type = type;
    sec->flags = flags;
    sec->buff = NULL;
// sec->addr
// sec->offset Set after buffer allocation in pass Comp
// sec->link
// sec->entsize
    sec->next = NULL;

    ctx->shnum++;
    sec->sectionIndex = ctx->shnum;

    ctx->sectionTail->next = sec;
    ctx->sectionTail = sec;
    ctx->section = sec;

    // Write Name to shstrtab
    sec->name_offset = ctx->shstrtab->index;
    memcpy(ctx->shstrtab->buff + ctx->shstrtab->index, ctx->token->buff, ctx->token->buffTop - ctx->token->buff);
    ctx->shstrtab->index += ctx->token->buffTop - ctx->token->buff;
    ctx->shstrtab->buff[ctx->shstrtab->index] = '\0';
    ctx->shstrtab->index++;

  }
}

void compPass(CompContext*ctx){

  ctx->token = ctx->tokenHead;
  ctx->section = NULL;


  bool mode_text=false,mode_data=false,mode_test=false;
  while(ctx->token){
   

    if(ctx->token->type == Newline){
      ctx->token = ctx->token->next;
      continue;
    }

    if(ctx->token->type == Identifier && ctx->token->next && ctx->token->next->type == Doubledot){
      // Symbol
 	if(ctx->pass == INDEX_BUFFERS){
	  ctx->symtab->size += sizeof(Elf32_Sym);
	}else if(ctx->pass == COMP){
//	  Elf32_Sym*sym =(Elf32_Sym*)(ctx->symtab->buff + ctx->symtab->index);
//	  sym->st_name = ctx->strtab->index;
//	  sym->st_value = ctx->section->index;
//	  sym->st_size = 0;
//	  sym->st_info = 0;
	  ctx->symtab->index += sizeof(Elf32_Sym);
	}
	ctx->token = ctx->token->next->next;
	continue;
    }


    // Directive
    if(tokenIdentComp(".text",ctx->token)){
      setSection(ctx,SHT_PROGBITS,SHF_ALLOC|SHF_EXECINSTR);
      mode_text = true;
      mode_data = false;
      mode_test = true;
      ctx->token=ctx->token->next;
      continue;
    }

    if(tokenIdentComp(".data",ctx->token)){
      setSection(ctx,SHT_PROGBITS,SHF_ALLOC|SHF_WRITE);
      mode_text = false;
      mode_data = true;
      mode_test = true;
      ctx->token = ctx->token->next;
      continue;
    }
    if(mode_text){
//	ctx->token = ctx->token->next;
//	continue;
    }

    if(mode_data){
//	ctx->token = ctx->token->next;
//	continue;
    }

    if(mode_test){
      if(tokenIdentComp("TestData",ctx->token)){
	if(ctx->pass == INDEX_BUFFERS){
	  ctx->section->size += 8;
	}
	else if(ctx->pass== COMP){
	  if(ctx->section->index+8<ctx->section->size)compError("Out Of Buff Memory",ctx->token);
	  *((uint64_t*)(ctx->section->buff + ctx->section->index)) = 0xaabbccdd;
	  ctx->section->index += 8;
	}
	ctx->token = ctx->token->next;
	continue;
      }
    }


    compError("Unexpected Token in Main Switch",ctx->token);
  }
}


void comp(char*inputfilename,char*outputfilename){
  // Create CompContext
  CompContext*ctx = malloc(sizeof(CompContext));

  // Tokenize File
  ctx->tokenHead = tokenizeFile(inputfilename);

  // Init Sections
  ctx->shnum = 4;
  ctx->sectionHead = calloc(sizeof(Section),1);
  Section*shstrtab = malloc(sizeof(Section));
  Section*strtab = malloc(sizeof(Section));
  Section*symtab = malloc(sizeof(Section));
  // Link Sections
  ctx->shstrtab = shstrtab;
  ctx->strtab = strtab;
  ctx->symtab = symtab;

  ctx->sectionHead->next = ctx->shstrtab;
  ctx->shstrtab->next = strtab;
  ctx->strtab->next = symtab;
  ctx->symtab->next = NULL;
  ctx->sectionTail = symtab;
  // Init shtrtab
  shstrtab->size = 27;
  shstrtab->index = 0;
  shstrtab->sectionIndex = 1;
  shstrtab->buff = NULL;
  shstrtab->type = SHT_STRTAB;
  shstrtab->name_offset = 1;

  strtab->name_offset = 11;
  strtab->index = 0;
  strtab->size = 0;
  strtab->buff = NULL;
  strtab->type = SHT_STRTAB;
  strtab->sectionIndex = 2;

  symtab->name_offset = 19;
  symtab->index = 0;
  symtab->size = 0;
  symtab->buff = NULL;
  symtab->type = SHT_SYMTAB;
  symtab->sectionIndex = 3;
  symtab->link = 2;


  // Index Sections Pass
  ctx->pass = INDEX_SECTIONS;
  compPass(ctx);

  // Allocate shstrtab Buffer and insert its own name
  ctx->shstrtab->buff = malloc(ctx->shstrtab->size);
  memcpy(ctx->shstrtab->buff,"\0.shstrtab\0.strtab\0.symtab\0",27);
  ctx->shstrtab->index += 27;

  // Index_Buffers Pass: Create Sections and estimate Buffer Sizes
  ctx->pass = INDEX_BUFFERS;
  compPass(ctx);

  // Allocate remaining section Buffers 
  for(Section*sec = ctx->sectionHead->next;sec;sec=sec->next){
    if(!sec->buff){
      sec->buff = malloc(sec->size);
    }
  }

  // Comp Pass
  ctx->pass = COMP;
  compPass(ctx);

  // Set Section Offset
  ctx->sectionHead->next->offset = sizeof(Elf32_Ehdr) + sizeof(Elf32_Shdr) * ctx->shnum;
  for(Section*sec = ctx->sectionHead->next;sec->next;sec=sec->next){
    sec->next->offset = sec->offset + sec->index;
  }

  // Print
  for(Section*sec = ctx->sectionHead;sec;sec=sec->next)    
    printf("Section %s\t size=%ld\t offset=%ld\n",
	ctx->shstrtab->buff+sec->name_offset,
	sec->size,
	sec->offset);

  // Export
  export_elf(ctx,outputfilename);

}

