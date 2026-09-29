// Export every discovered function, including failed decompilation records.
// @category MTvGPU
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.framework.Application;
import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.time.Instant;
import java.util.*;

public class ExportDriverCorpus extends GhidraScript {
    private final Gson gson = new GsonBuilder().disableHtmlEscaping().create();
    private Path out;
    private int total, attempted, succeeded, failed, external, thunks;
    private long start;

    private BufferedWriter writer(String file) throws Exception {
        return Files.newBufferedWriter(out.resolve(file), StandardCharsets.UTF_8);
    }
    private void line(BufferedWriter w, Object value) throws Exception {
        w.write(gson.toJson(value)); w.newLine();
    }
    private Map<String,Object> record(Object... pairs) {
        Map<String,Object> m = new LinkedHashMap<>();
        for (int i=0; i<pairs.length; i+=2) m.put((String)pairs[i], pairs[i+1]);
        return m;
    }
    private void progress(String phase, String current) throws Exception {
        Map<String,Object> m = record("phase", phase, "program", currentProgram.getName(),
            "ghidra_version", Application.getApplicationVersion(),
            "language", currentProgram.getLanguageID().toString(),
            "compiler", currentProgram.getCompilerSpec().getCompilerSpecID().toString(),
            "image_base", currentProgram.getImageBase().toString(),
            "total_functions", total, "attempted", attempted, "succeeded", succeeded,
            "failed", failed, "external_functions", external, "thunk_functions", thunks,
            "current_function", current, "timestamp", Instant.now().toString(),
            "elapsed_seconds", (System.currentTimeMillis()-start)/1000);
        Path temp=out.resolve("progress.json.tmp");
        Files.writeString(temp, gson.toJson(m)+"\n", StandardCharsets.UTF_8);
        Files.move(temp,out.resolve("progress.json"),StandardCopyOption.REPLACE_EXISTING,StandardCopyOption.ATOMIC_MOVE);
    }
    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) throw new IllegalArgumentException("Expected output directory");
        out=Path.of(args[0]); Files.createDirectories(out); start=System.currentTimeMillis();
        total=currentProgram.getFunctionManager().getFunctionCount();
        progress("exporting_metadata", "");
        try (BufferedWriter w=writer("symbols.jsonl")) {
            SymbolIterator it=currentProgram.getSymbolTable().getAllSymbols(true);
            while(it.hasNext()) {
                monitor.checkCancelled(); Symbol s=it.next();
                line(w,record("address",s.getAddress().toString(),"name",s.getName(true),
                    "type",s.getSymbolType().toString(),"source",s.getSource().toString(),
                    "external",s.isExternal(),"entry_point",currentProgram.getSymbolTable().isExternalEntryPoint(s.getAddress())));
            }
        }
        try (BufferedWriter w=writer("strings.jsonl")) {
            DataIterator it=currentProgram.getListing().getDefinedData(true);
            while(it.hasNext()) {
                monitor.checkCancelled(); Data d=it.next(); Object value=d.getValue();
                if (!(value instanceof String)) continue;
                List<String> refs=new ArrayList<>();
                ReferenceIterator ri=currentProgram.getReferenceManager().getReferencesTo(d.getAddress());
                while(ri.hasNext()) refs.add(ri.next().getFromAddress().toString());
                line(w,record("address",d.getAddress().toString(),"value",value,"references_from",refs));
            }
        }
        DecompInterface decompiler=new DecompInterface();
        DecompileOptions options=new DecompileOptions(); options.grabFromProgram(currentProgram);
        decompiler.setOptions(options); decompiler.toggleCCode(true); decompiler.toggleSyntaxTree(false);
        decompiler.setSimplificationStyle("decompile");
        if (!decompiler.openProgram(currentProgram)) throw new IOException(decompiler.getLastMessage());
        try (BufferedWriter code=writer("decompiled.c"); BufferedWriter index=writer("functions.jsonl");
             BufferedWriter failures=writer("failures.jsonl"); BufferedWriter calls=writer("calls.jsonl")) {
            code.write("/* Ghidra pseudocode; not original source and not buildable. */\n");
            long cLine=2;
            FunctionIterator externals=currentProgram.getFunctionManager().getExternalFunctions();
            while(externals.hasNext()) {
                Function f=externals.next(); external++;
                line(index,record("address",f.getEntryPoint().toString(),"name",f.getName(true),
                    "signature",f.getSignature().toString(),"external",true,"status","external"));
            }
            FunctionIterator it=currentProgram.getFunctionManager().getFunctions(true);
            while(it.hasNext()) {
                monitor.checkCancelled(); Function f=it.next();
                String address=f.getEntryPoint().toString();
                Map<String,Object> entry=record("address",address,"name",f.getName(true),
                    "signature",f.getSignature().toString(),"body_bytes",f.getBody().getNumAddresses(),
                    "external",f.isExternal(),"thunk",f.isThunk());
                if (f.isExternal()) { external++; entry.put("status","external"); line(index,entry); continue; }
                if (f.isThunk()) thunks++;
                progress("decompiling",address+" "+f.getName());
                attempted++;
                String c=null; String error=""; boolean retried=false;
                try {
                    DecompileResults result=decompiler.decompileFunction(f,60,monitor);
                    if (!result.decompileCompleted()) {
                        error=result.getErrorMessage(); retried=true;
                        decompiler.resetDecompiler();
                        result=decompiler.decompileFunction(f,180,monitor);
                    }
                    if (result.decompileCompleted() && result.getDecompiledFunction()!=null) {
                        c=result.getDecompiledFunction().getC();
                        error=result.getErrorMessage();
                    } else error=result.getErrorMessage();
                } catch (Exception e) { error=e.toString(); decompiler.resetDecompiler(); }
                entry.put("retried",retried); entry.put("message",error);
                String header="\n/* FUNCTION "+address+" "+f.getName(true).replace("*/","* /")+" */\n";
                String text=header+(c==null ? "/* DECOMPILATION FAILED; see failures.jsonl and disassembly.txt. */\n" : c+"\n");
                long lines=text.chars().filter(ch->ch=='\n').count();
                entry.put("c_line_start",cLine); entry.put("c_line_count",lines);
                entry.put("status",c==null ? "failed" : "decompiled");
                code.write(text); cLine+=lines;
                if(c==null) { failed++; line(failures,entry); failures.flush(); } else succeeded++;
                line(index,entry);
                List<String> targets=new ArrayList<>();
                for(Function target:f.getCalledFunctions(monitor)) targets.add(target.getEntryPoint().toString());
                line(calls,record("from",address,"to",targets));
                if(attempted%100==0) {
                    code.flush();index.flush();calls.flush();decompiler.flushCache();
                    println("EXPORTED "+attempted+"/"+total+" failed="+failed);
                }
            }
        } finally { decompiler.dispose(); }
        progress("completed", "");
        println("CORPUS_EXPORT_COMPLETE attempted="+attempted+" succeeded="+succeeded+" failed="+failed);
    }
}
