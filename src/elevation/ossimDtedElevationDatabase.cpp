#include <ossim/elevation/ossimDtedElevationDatabase.h>
#include <ossim/base/ossimDirectory.h>
#include <ossim/base/ossimGeoidManager.h>
#include <ossim/base/ossimNotify.h>
#include <ossim/base/ossimPreferences.h>
#include <ossim/base/ossimTrace.h>
#include <sstream>
#include <iomanip>
#include <cstdlib> /* for abs(int) */

static ossimTrace traceDebug("ossimDtedElevationDatabase:debug");
RTTI_DEF1(ossimDtedElevationDatabase, "ossimDtedElevationDatabase", ossimElevationCellDatabase);

ossimDtedElevationDatabase::ossimDtedElevationDatabase()
   : ossimElevationCellDatabase(),
     m_extension(""),
     m_upcase(false),
     m_lastHandler(0),
     m_mutex()
{
}

ossimDtedElevationDatabase::ossimDtedElevationDatabase(const ossimDtedElevationDatabase& rhs)
   : ossimElevationCellDatabase(rhs),
     m_extension(rhs.m_extension),
     m_upcase(rhs.m_upcase),
     m_lastHandler(0), // Do not copy this to get a unique handler for thread.
     m_mutex()
{
}

ossimDtedElevationDatabase::~ossimDtedElevationDatabase()
{
}

ossimObject* ossimDtedElevationDatabase::dup() const
{
   ossimDtedElevationDatabase* duped = new ossimDtedElevationDatabase(*this);
   return duped;
}

double ossimDtedElevationDatabase::getHeightAboveMSL(const ossimGpt& gpt)
{
   if(!isSourceEnabled())
      return ossim::nan();
   std::lock_guard<std::mutex> lock(m_mutex);
   double result = ossim::nan();
   if(m_lastHandler.valid() && m_lastHandler->pointHasCoverage(gpt))
   {
      result = m_lastHandler->getHeightAboveMSL(gpt);
   }
   else
   {
      m_lastHandler = getOrCreateCellHandler(gpt);
      if(m_lastHandler.valid())
         result = m_lastHandler->getHeightAboveMSL(gpt);
   }

   return result;
}

double ossimDtedElevationDatabase::getHeightAboveEllipsoid(const ossimGpt& gpt)
{
   double h = getHeightAboveMSL(gpt);
   if(h != ossim::nan())
   {
      double offset = getOffsetFromEllipsoid(gpt);
      
      h += offset;
   }
   
   return h;
}
bool ossimDtedElevationDatabase::open(const ossimString& connectionString)
{
   bool result = false;
   ossimFilename file = ossimFilename(connectionString);
   
   result = openDtedDirectory(file);

   return result;
}

bool ossimDtedElevationDatabase::openDtedDirectory(const ossimFilename& dir)
{
   if(traceDebug())
   {
      ossimNotify(ossimNotifyLevel_DEBUG)
         << "ossimDtedElevationDatabase::open entered ...\n"
         << "dir: " << dir << "\n";
   }
   
   bool result = dir.isDir();
   if(result)
   {
      if ( m_extension.size() == 0 )
      {
         //---
         // This sets extension by doing a directory scan and is now depricated.
         // Use "extension" key in preferences to avoid this.  Example:
         // elevation_manager.elevation_source0.extension: dt2
         //---
         result = inititializeExtension( dir );
         if ( !result && traceDebug() )
         {
            ossimNotify(ossimNotifyLevel_DEBUG)
               << "ossimDtedElevationDatabase::open: WARNING "
               << "Scan for dted extension failed!\n"
               << "Can be set in ossim preferences.  Example:\n"
               << "elevation_manager.elevation_source0.extension: .dt2\n";
         }
      }
      
      // Set the geoid:
      if( !m_geoid.valid() )
      {
         m_geoid = ossimGeoidManager::instance()->findGeoidByShortName("geoid1996", false);
         if(!m_geoid.valid()&&traceDebug())
         {
            ossimNotify(ossimNotifyLevel_DEBUG)
               << "ossimDtedElevationDatabase::open: WARNING "
               << "Unable to load goeid grid 1996 for DTED database\n";
         }
      }

   }

   if(traceDebug())
   {
      ossimNotify(ossimNotifyLevel_DEBUG)
         << "ossimDtedElevationDatabase::open result:" << (result?"true":"false") << "\n";
   }
   return result;
}

bool ossimDtedElevationDatabase::getAccuracyInfo(ossimElevationAccuracyInfo& info, const ossimGpt& gpt) const
{
   bool result = false;
   
   m_mutex.lock();
   ossimDtedElevationDatabase* thisPtr = const_cast<ossimDtedElevationDatabase*>(this);
   ossimRefPtr<ossimElevCellHandler> tempHandler = thisPtr->getOrCreateCellHandler(gpt);
   m_mutex.unlock();

   if(tempHandler.valid())
   {
      result = tempHandler->getAccuracyInfo(info, gpt);
   }
  return result;
}

void ossimDtedElevationDatabase::createRelativePath(ossimFilename& file, const ossimGpt& gpt)const
{
   ossimFilename lon, lat;
   int ilon = static_cast<int>(floor(gpt.lond()));
   
   if (ilon < 0)
   {
      lon = m_upcase?"W":"w";
   }
   else
   {
      lon = m_upcase?"E":"e";
   }
   
   ilon = abs(ilon);
   std::ostringstream  s1;
   s1 << std::setfill('0') << std::setw(3)<< ilon;
   
   lon += s1.str().c_str();//ossimString::toString(ilon);
   
   int ilat =  static_cast<int>(floor(gpt.latd()));
   if (ilat < 0)
   {
      lat += m_upcase?"S":"s";
   }
   else
   {
      lat += m_upcase?"N":"n";
   }
   
   ilat = abs(ilat);
   std::ostringstream  s2;
   
   s2<< std::setfill('0') << std::setw(2)<< ilat;
   
   lat += s2.str().c_str();
   
   file = lon.dirCat(lat+m_extension);
}

ossimRefPtr<ossimElevCellHandler> ossimDtedElevationDatabase::createCell(const ossimGpt& gpt)
{
   ossimRefPtr<ossimElevCellHandler> result = 0;
   ossimFilename f;
   createFullPath(f, gpt);
   if(f.exists())
   {
      ossimRefPtr<ossimDtedHandler> h = new ossimDtedHandler(f, m_memoryMapCellsFlag);
      if (!(h->getErrorStatus()))
      {
         result = h.get();
      }
   }
   return result;
}

bool ossimDtedElevationDatabase::loadState(const ossimKeywordlist& kwl, const char* prefix )
{
   bool result = ossimElevationCellDatabase::loadState(kwl, prefix);
   if(result)
   {
      if(!m_connectionString.empty()&&ossimFilename(m_connectionString).exists())
      {
         // Look for "extension" keyword.
         std::string pref = (prefix?prefix:"");
         std::string key = "extension";
         ossimString val = ossimPreferences::instance()->preferencesKWL().findKey( pref, key );
         if ( val.size() )
         {
            if ( val.string()[0] != '.' )
            {
               m_extension = ".";
               m_extension += val;
               
               ossimNotify(ossimNotifyLevel_WARN)
                  << "\nossimDtedElevationDatabase::loadState: WARNING\n"
                  << "Key value for \"extension\" does not start with a dot!\n"
                  << "Consider changing \"" << val << "\" to \"" << m_extension << "\"\n"
                  << std::endl;   
            }
            else
            {
               m_extension = val;
            }
         }
         else if ( traceDebug() )
         {
            ossimNotify(ossimNotifyLevel_DEBUG)
               << "\nossimDtedElevationDatabase::loadState: NOTICE\n"
               << "Key lookup for \"extension\" failed!\n"
               << "Can be set in ossim preferences.  Example:\n"
               << pref << key << ": .dt2\n\n";
         }

         key = "upcase";
         val = ossimPreferences::instance()->preferencesKWL().findKey( pref, key );
         if ( val.size() )
         {
            m_upcase = val.toBool();
         }
         else if ( traceDebug() )
         {
            ossimNotify(ossimNotifyLevel_DEBUG)
               << "\nossimDtedElevationDatabase::loadState: NOTICE\n"
               << "Key lookup for \"upcase\" failed!\n"
               << "Can be set in ossim preferences.  Example:\n"
               << pref << key << ": false\n\n";
         }
         

         result = open(m_connectionString);
      }
      else
      {
         // can't open the connection because it does not exists or empty
         result = false;
      }
   }
   
   return result;
}

bool ossimDtedElevationDatabase::saveState(ossimKeywordlist& kwl, const char* prefix)const
{
   kwl.add(prefix, "extension", m_extension, true);
   kwl.add(prefix, "upcase", m_upcase, true);
   
   bool result = ossimElevationCellDatabase::saveState(kwl, prefix);
   
   return result;
}

std::ostream& ossimDtedElevationDatabase::print(ostream& out) const
{
   ossimKeywordlist kwl;
   saveState(kwl);
   out << "\nossimDtedElevationDatabase @ "<< (ossim_uint64) this << "\n"
         << kwl <<ends;
   return out;
}

bool ossimDtedElevationDatabase::inititializeExtension( const ossimFilename& dir )
{
   ossim_uint32 count = 0;
   ossim_uint32 maxCount = 10;
   ossimDirectory od;
   bool result = od.open(dir);
   if(result)
   {
      result = false;
      ossimFilename f;
      // Get the first directory.
      od.getFirst(f, ossimDirectory::OSSIM_DIR_DIRS);
      
      do
      {
         ++count;
         // Must be a directory.
         if (f.isDir())
         {
            // Discard any full path.
            ossimFilename fileOnly = f.file();
            
            // Downcase it. Note have sites with upper case. (drb)
            // fileOnly.downcase();

            //---
            // Longitude subdir check:
            // Must start with 'e', 'E', 'w' or 'W'.
            //---
            bool foundCell = ( ( (fileOnly.c_str()[0] == 'e') ||
                                 (fileOnly.c_str()[0] == 'w') ||
                                 (fileOnly.c_str()[0] == 'E') ||
                                 (fileOnly.c_str()[0] == 'W') ) &&
                               (fileOnly.size() == 4));
            if(foundCell)
            {
               ossim_uint32 maxCount2 = 10;
               ossim_uint32 count2 = 0;
               ossimDirectory d2;

               // Open the longitude subdir:
               if(d2.open(f))
               {
                  d2.getFirst(f, ossimDirectory::OSSIM_DIR_FILES);
                  do
                  {
                     ossimRefPtr<ossimDtedHandler> dtedHandler = new ossimDtedHandler();
                     if(dtedHandler->open(f, false))
                     {
                        if(traceDebug())
                        {
                           ossimNotify(ossimNotifyLevel_DEBUG)
                              << "ossimDtedElevationDatabase::open: Found dted file " << f << "\n";
                        }
                        result = true;
                        m_extension = "."+f.ext();
                        m_connectionString = dir;
                        m_meanSpacing = dtedHandler->getMeanSpacingMeters();
                     }
                     dtedHandler->close();
                     dtedHandler = 0;
                     ++count2;
                  } while(!result&&d2.getNext(f)&&(count2 < maxCount2));
               }
            }
         }
      } while(!result&&(od.getNext(f))&&(count < maxCount));
   }

   return result;
   
} // End: ossimDtedElevationDatabase::inititializeExtension( dir )
