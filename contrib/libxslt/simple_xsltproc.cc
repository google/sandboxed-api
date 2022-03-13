/*
 * libxslt_tutorial.c: demo program for the XSL Transformation 1.0 engine
 *
 * based on xsltproc.c, by Daniel.Veillard@imag.fr
 * by John Fleck
 *
 * See Copyright for the status of this software.
 *
 */

#include <err.h>
#include <fcntl.h>
#include <libxml/catalog.h>
#include <libxml/debugXML.h>
#include <libxml/parser.h>
#include <libxml/xinclude.h>
#include <libxml/xmlIO.h>
#include <libxml/xmlmemory.h>
#include <libxslt/transform.h>
#include <libxslt/xslt.h>
#include <libxslt/xsltInternals.h>
#include <libxslt/xsltutils.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <iostream>

#include "libxslt_sapi.h"

[[noreturn]] static void usage(const char* name, int status) {
  printf("Usage: %s [options] stylesheet file [file ...]\n", name);
  printf("      --param name value : pass a (parameter,value) pair\n");
  exit(status);
}

absl::Status main_(int argc, char** argv) {
  int i;
  const char* params[16 + 1];
  int nbparams = 0;
  sapi::contrib::libxslt::LibXsltSapiSandbox sandbox{};
  sapi::contrib::libxslt::LibXSLTApi api(&sandbox);

  if (argc < 1 || !**argv) {
    fputs("NULL or empty argv[0], failing\n", stderr);
    exit(1);
  }
  if (argc <= 1) usage(argv[0], 1);
  if (!strcmp(argv[1], "--help")) usage(argv[0], 0);
  for (i = 1; i < argc; i++) {
    if (argv[i][0] != '-') break;
    if (!strcmp(argv[i], "--param")) {
      if (argc - i < 2) {
        fprintf(stderr, "missing arguments to --param\n");
        exit(1);
      }
      params[nbparams++] = argv[++i];
      params[nbparams++] = argv[++i];
      if (nbparams >= 16) {
        fprintf(stderr, "too many params (limit 16)\n");
        usage(argv[0], 1);
      }
    } else if (!strcmp(argv[i], "--")) {
      i++;
      break;
    } else {
      fprintf(stderr, "Unknown option %s\n", argv[i]);
      usage(argv[0], 1);
    }
  }
  if (argc - i != 2) usage(argv[0], 1);
  params[nbparams] = NULL;

  int stylesheetFd = open(argv[i], O_CLOEXEC | O_NOCTTY | O_RDONLY, 0);
  if (stylesheetFd == -1) err(1, "cannot open stylesheet");
  int documentFd = open(argv[i + 1], O_CLOEXEC | O_NOCTTY | O_RDONLY, 0);
  if (documentFd == -1) err(1, "cannot open document");
  SAPI_RETURN_IF_ERROR(sandbox.Init());
  SAPI_RETURN_IF_ERROR(api.xmlInitParser());
  sapi::v::Array ptrs(params, nbparams + 1);

  sapi::v::Fd fStylesheet{stylesheetFd}, fDocument{documentFd}, fStdout{0};
  documentFd = stylesheetFd = -1;

  SAPI_RETURN_IF_ERROR(sandbox.TransferToSandboxee(&fStylesheet));
  SAPI_RETURN_IF_ERROR(sandbox.TransferToSandboxee(&fDocument));
  SAPI_RETURN_IF_ERROR(sandbox.TransferToSandboxee(&fStdout));
  ::sapi::v::ConstCStr encoding{"UTF-8"};
  ::sapi::v::NullPtr null;
  SAPI_ASSIGN_OR_RETURN(
      auto vDocumentRaw,
      api.xmlReadFd(fDocument.GetRemoteFd(), &null, encoding.PtrBefore(), 0));
  if (!vDocumentRaw) errx(1, "xmlReadFd() failed for document");
  SAPI_ASSIGN_OR_RETURN(
      auto vStylesheetRaw,
      api.xmlReadFd(fStylesheet.GetRemoteFd(), &null, encoding.PtrBefore(), 0));
  if (!vStylesheetRaw) errx(1, "xmlReadFd() failed for document");
  sapi::v::RemotePtr vStylesheet{vStylesheetRaw}, vDocument{vDocumentRaw};
  SAPI_ASSIGN_OR_RETURN(auto vTransformedRaw,
                        api.sapi_xsltParseStylesheetDoc(
                            &vStylesheet, &vDocument, ptrs.PtrBefore()));
  sapi::v::RemotePtr vTransformed{vTransformedRaw};
  SAPI_ASSIGN_OR_RETURN(auto res, api.xmlSaveToFd(fStdout.GetRemoteFd(), encoding.PtrBefore(), 0));
  sapi::v::RemotePtr vSaveCtx{res};
  SAPI_ASSIGN_OR_RETURN(auto save_res, api.xmlSaveDoc(&vSaveCtx, &vTransformed));
  SAPI_ASSIGN_OR_RETURN(save_res, api.xmlSaveFlush(&vSaveCtx));
  SAPI_ASSIGN_OR_RETURN(save_res, api.xmlSaveClose(&vSaveCtx));
  return absl::OkStatus();
}

int main(int argc, char* argv[]) {
  auto status = main_(argc, argv);
  if (status.ok()) return 0;
  std::cerr << status << std::endl;
  return 1;
}
