using System.Reflection;
using System.Collections;
// Проверка реальных этапов импорта без открытия окна или записи проекта пользователя.
class Program {
 const string Dir="C:/Program Files/Owen/Owen Logic";
 static Assembly MainAssembly=null!;
 static object New(string name,params object?[] args)=>Activator.CreateInstance(MainAssembly.GetType(name,true)!,BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic,null,args,null)!;
 static object? Call(object obj,string name,params object?[] args)=>obj.GetType().GetMethod(name,BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance)!.Invoke(obj,args);
 [STAThread] static int Main(string[] args) {
  Console.OutputEncoding=new System.Text.UTF8Encoding(false);
  AppDomain.CurrentDomain.AssemblyResolve+=(s,e)=>{
   var n=new AssemblyName(e.Name).Name+".dll";
   foreach(var dir in new[]{Dir,Dir+"/ST"})if(File.Exists(dir+"/"+n))return Assembly.LoadFrom(dir+"/"+n);
   return null;
  };
  try {
   MainAssembly=Assembly.LoadFrom(Dir+"/ProgramRelayFBD.dll");
   var pr=Assembly.LoadFrom(Dir+"/Owen.ProgrammableRelay.dll");
   var ext=Assembly.LoadFrom(Dir+"/Owen.ExternalData.dll");
   var serializerType=pr.GetType("Owen.ProgrammableRelay.DeviceDescriptionSerializer",true)!;
   var xml=serializerType.GetMethod("CreateInstance",BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Static)!.Invoke(null,null);
   using var stream=ext.GetManifestResourceStream("Owen.ExternalData.macro.tag");
   var description=pr.GetType("Owen.ProgrammableRelay.DeviceDescription",true)!.GetMethod("LoadDescription")!.Invoke(null,new[]{stream,xml});
   var target=New("ProgramRelayFBD.DomainLayer.DeviceTarget",description,null);
   var repo=New("ProgramRelayFBD.InfrastructureLayer.Adapters.DeviceTargetRepository");Call(repo,"AddTarget",target);
   var reader=New("ProgramRelayFBD.InfrastructureLayer.Adapters.Macros.MacroTemplateReader",repo);
   using var file=File.OpenRead(args[0]);
   var task=(Task)Call(reader,"ReadFromAsync",file)!;task.GetAwaiter().GetResult();
   var macros=(IEnumerable)task.GetType().GetProperty("Result")!.GetValue(task)!;
   var visitorType=MainAssembly.GetType("ProgramRelayFBD.InfrastructureLayer.Adapters.RenameVariablesInDocumentVisitor",true)!;
   var visitors=Array.CreateInstance(visitorType,1);visitors.SetValue(Activator.CreateInstance(visitorType),0);
   var procType=MainAssembly.GetType("ProgramRelayFBD.InfrastructureLayer.Adapters.Serialization.AdjustProcessors.IDocumentStoreModelProcessor",true)!;
   var docSerializer=New("ProgramRelayFBD.InfrastructureLayer.Adapters.ContainersVer2.Serialization.DocumentModelSerializer",visitors,Array.CreateInstance(procType,0));
   var drawingRepo=New("ProgramRelayFBD.InfrastructureLayer.Adapters.DrawingDocumentRepository",repo,
      New("ProgramRelayFBD.InfrastructureLayer.Adapters.ProjectSettingsModelRepository"),
      New("ProgramRelayFBD.InfrastructureLayer.Adapters.ContainersVer2.VariableUniqueIdModificatorRepository"),docSerializer);
   foreach(var macro in macros){
    Console.WriteLine("LOADED: "+macro.GetType().GetProperty("DocumentName")!.GetValue(macro));
    ((Task)Call(drawingRepo,"Store",macro)!).GetAwaiter().GetResult();Console.WriteLine("STORED");
    var uid=macro.GetType().GetProperty("UniqueId")!.GetValue(macro);
    var reload=(Task)Call(drawingRepo,"LoadMacroByUniqueId",uid)!;reload.GetAwaiter().GetResult();
    if(reload.GetType().GetProperty("Result")!.GetValue(reload)==null)throw new InvalidOperationException("Повторное чтение не вернуло макрос.");
    var et=MainAssembly.GetType("ProgramRelayFBD.Macroes.ElementMacro",true)!;
    var el=et.GetMethod("Create",BindingFlags.Static|BindingFlags.Public|BindingFlags.NonPublic)!.Invoke(null,new[]{macro});
    var init=el!.GetType().GetMethod("Initialize",BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance)!;init.Invoke(el,init.GetParameters().Select(p=>p.HasDefaultValue?p.DefaultValue:Type.Missing).ToArray());Console.WriteLine("LIBRARY_ELEMENT_CREATED");
   }
   return 0;
  } catch(Exception e) {Console.WriteLine(e);return 1;}
 }
}
